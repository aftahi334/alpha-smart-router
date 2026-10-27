#include "alpha/routing/path_selection.hpp"

namespace alpha::routing {

namespace {
struct XorShift32 {
    std::uint32_t state;
    explicit XorShift32(std::uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t next() noexcept { auto x=state; x^=x<<13; x^=x>>17; x^=x<<5; return state=x; }
    std::uint32_t next_bounded(std::uint32_t b) noexcept { return b? next()%b : 0u; }
};
}

// --------- CP / DP seqlock ops ---------

void cp::update_metrics(MetricsSlot& s, const PathMetrics& m) noexcept {
    const auto start = s.seq.load(std::memory_order_relaxed);
    const auto odd   = start | 1u;
    s.seq.store(odd, std::memory_order_relaxed); // writer enters (odd)
    std::atomic_thread_fence(std::memory_order_release);

    s.rtt_us.store(m.rtt_us, std::memory_order_relaxed);
    s.one_way_delay_us.store(m.one_way_delay_us, std::memory_order_relaxed);
    s.jitter_us.store(m.jitter_us, std::memory_order_relaxed);
    s.loss_ppm.store(m.loss_ppm, std::memory_order_relaxed);
    s.avail_kbps.store(m.avail_kbps, std::memory_order_relaxed);
    s.qos_class.store(m.qos_class, std::memory_order_relaxed);
    s.healthy.store(m.healthy, std::memory_order_relaxed);

    s.seq.store(odd + 1u, std::memory_order_release); // publish (even)
}

bool dp::load_metrics(const MetricsSlot& s, PathMetrics& out) noexcept {
    for (int i = 0; i < 4; ++i) {
        // Acquire pairs with writer's release; even => candidate stable snapshot.
        const auto s1 = s.seq.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0u) continue; // writer active

        PathMetrics snap{};
        snap.rtt_us           = s.rtt_us.load(std::memory_order_relaxed);
        snap.one_way_delay_us = s.one_way_delay_us.load(std::memory_order_relaxed);
        snap.jitter_us        = s.jitter_us.load(std::memory_order_relaxed);
        snap.loss_ppm         = s.loss_ppm.load(std::memory_order_relaxed);
        snap.avail_kbps       = s.avail_kbps.load(std::memory_order_relaxed);
        snap.qos_class        = s.qos_class.load(std::memory_order_relaxed);
        snap.healthy          = s.healthy.load(std::memory_order_relaxed);

        // Ensure relaxed payload loads complete before rechecking seq.
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto s2 = s.seq.load(std::memory_order_relaxed);
        if (s1 == s2) {
            out = snap;
            return true;
        }
    }
    return false;
}

bool qos_match(std::uint8_t path_class, std::uint8_t /*dscp*/) noexcept {
    return path_class != 0; // placeholder: treat non-zero class as a weak match
}

// ---------------- Policies ----------------

FlowHashPolicy::FlowHashPolicy(bool skip_unhealthy) noexcept
: skip_unhealthy_(skip_unhealthy) {}

PathId RoundRobinPolicy::choose(std::span<const CandidateRef> cands,
                                const PacketContext&) noexcept {
    const auto n = static_cast<std::uint32_t>(cands.size());
    if (n == 0) return 0;
    const auto start = idx_.fetch_add(1, std::memory_order_relaxed) % n;
    PathMetrics m{};
    for (std::uint32_t i=0;i<n;++i) {
        const auto k = (start + i) % n;
        if (cands[k].slot && dp::load_metrics(*cands[k].slot, m) && m.healthy) return cands[k].id;
    }
    return cands[start].id; // degraded
}

PathId FlowHashPolicy::choose(std::span<const CandidateRef> cands,
                              const PacketContext& pkt) noexcept {
    const auto n = static_cast<std::uint32_t>(cands.size());
    if (n == 0) return 0;
    const auto base = pkt.flow_hash % n;
    if (!skip_unhealthy_) return cands[base].id;

    PathMetrics m{};
    for (std::uint32_t i=0;i<n;++i) {
        const auto k = (base + i) % n;
        if (cands[k].slot && dp::load_metrics(*cands[k].slot, m) && m.healthy) return cands[k].id;
    }
    return cands[base].id; // keep mapping stable if all unhealthy
}

LatencyAwarePolicy::LatencyAwarePolicy(LatencyAwareConfig cfg) noexcept : cfg_(cfg) {}

PathId LatencyAwarePolicy::choose(std::span<const CandidateRef> cands,
                                  const PacketContext& pkt) noexcept {
    if (cands.empty()) return 0;

    // Min-RTT among healthy, QoS tie-break
    std::size_t best = 0; bool have_best=false; PathMetrics bestm{}; PathMetrics m{};
    for (std::size_t i=0;i<cands.size();++i) {
        if (!cands[i].slot || !dp::load_metrics(*cands[i].slot, m) || !m.healthy) continue;
        if (!have_best || m.rtt_us < bestm.rtt_us) { best=i; bestm=m; have_best=true; }
        else if (cfg_.prefer_qos_class) {
            const bool close = (m.rtt_us <= bestm.rtt_us + cfg_.tie_margin_us);
            if (close && qos_match(m.qos_class, pkt.dscp) && !qos_match(bestm.qos_class, pkt.dscp)) {
                best=i; bestm=m;
            }
        }
    }

    if (!have_best) {
        // No healthy: pick absolute min RTT deterministically
        std::size_t idx=0; PathMetrics minm{}; bool init=false;
        for (std::size_t i=0;i<cands.size();++i) {
            if (!cands[i].slot || !dp::load_metrics(*cands[i].slot, m)) continue;
            if (!init || m.rtt_us < minm.rtt_us) { idx=i; minm=m; init=true; }
        }
        return cands[init ? idx : 0].id;
    }

    // Optional exploration (disabled by default)
    if (cfg_.explore_ppm) {
        XorShift32 rng{pkt.flow_hash ^ salt_.load(std::memory_order_relaxed)};
        if (rng.next_bounded(1'000'000u) < cfg_.explore_ppm) {
            const auto n = static_cast<std::uint32_t>(cands.size());
            const auto start = rng.next_bounded(n);
            for (std::uint32_t i=0;i<n;++i) {
                const auto k = (start + i) % n;
                if (k != best && cands[k].slot && dp::load_metrics(*cands[k].slot, m) && m.healthy) {
                    salt_.fetch_add(0x9E37u, std::memory_order_relaxed);
                    return cands[k].id;
                }
            }
        }
    }
    return cands[best].id;
}

} // namespace alpha::routing
