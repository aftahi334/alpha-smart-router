#include "alpha/routing/path_selection.hpp"
#include "alpha/routing/qos_policy.hpp"

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

bool qos_match(std::uint8_t path_class, std::uint8_t dscp) noexcept {
    const auto path_tc = from_dp_qos_class(path_class);
    if (!path_tc.has_value()) return false;
    return *path_tc == classify_dscp(dscp);
}

// ---------------- Policies ----------------

FlowHashPolicy::FlowHashPolicy(bool skip_unhealthy,
                               bool allow_degraded_fallback) noexcept
    : skip_unhealthy_(skip_unhealthy),
      allow_degraded_fallback_(allow_degraded_fallback) {}

RouteDecision RoundRobinPolicy::choose_decision(std::span<const CandidateRef> cands,
                                                const PacketContext&) noexcept {
    const auto n = static_cast<std::uint32_t>(cands.size());
    if (n == 0) return RouteDecision{kInvalidPathId, SelectReason::NoCandidates};

    const auto start = idx_.fetch_add(1, std::memory_order_relaxed) % n;
    PathMetrics m{};
    bool any_loaded = false;
    std::uint32_t first_loaded_idx = 0;

    for (std::uint32_t i = 0; i < n; ++i) {
        const auto k = (start + i) % n;
        if (!cands[k].slot) continue;
        if (!dp::load_metrics(*cands[k].slot, m)) continue;
        if (!any_loaded) {
            any_loaded = true;
            first_loaded_idx = k;
        }
        if (m.healthy) {
            return RouteDecision{cands[k].id, SelectReason::SelectedHealthy};
        }
    }

    if (!any_loaded) {
        return RouteDecision{kInvalidPathId, SelectReason::SnapshotUnavailable};
    }
    if (!allow_degraded_fallback_) {
        return RouteDecision{kInvalidPathId, SelectReason::AllPathsUnhealthy};
    }
    return RouteDecision{cands[first_loaded_idx].id, SelectReason::DegradedFallback};
}

PathId RoundRobinPolicy::choose(std::span<const CandidateRef> cands,
                                const PacketContext& pkt) noexcept {
    return choose_decision(cands, pkt).path_id;
}

RouteDecision FlowHashPolicy::choose_decision(std::span<const CandidateRef> cands,
                                              const PacketContext& pkt) noexcept {
    const auto n = static_cast<std::uint32_t>(cands.size());
    if (n == 0) return RouteDecision{kInvalidPathId, SelectReason::NoCandidates};

    const auto base = pkt.flow_hash % n;
    if (!skip_unhealthy_) {
        return RouteDecision{cands[base].id, SelectReason::SelectedHealthy};
    }

    PathMetrics m{};
    bool any_loaded = false;
    std::uint32_t first_loaded_idx = 0;

    for (std::uint32_t i = 0; i < n; ++i) {
        const auto k = (base + i) % n;
        if (!cands[k].slot) continue;
        if (!dp::load_metrics(*cands[k].slot, m)) continue;
        if (!any_loaded) {
            any_loaded = true;
            first_loaded_idx = k;
        }
        if (m.healthy) {
            return RouteDecision{cands[k].id, SelectReason::SelectedHealthy};
        }
    }

    if (!any_loaded) {
        return RouteDecision{kInvalidPathId, SelectReason::SnapshotUnavailable};
    }
    if (!allow_degraded_fallback_) {
        return RouteDecision{kInvalidPathId, SelectReason::AllPathsUnhealthy};
    }
    return RouteDecision{cands[first_loaded_idx].id, SelectReason::DegradedFallback};
}

PathId FlowHashPolicy::choose(std::span<const CandidateRef> cands,
                              const PacketContext& pkt) noexcept {
    return choose_decision(cands, pkt).path_id;
}

LatencyAwarePolicy::LatencyAwarePolicy(LatencyAwareConfig cfg) noexcept : cfg_(cfg) {}

RouteDecision LatencyAwarePolicy::choose_decision(std::span<const CandidateRef> cands,
                                                  const PacketContext& pkt) noexcept {
    if (cands.empty()) {
        return RouteDecision{kInvalidPathId, SelectReason::NoCandidates};
    }

    // Min-RTT among healthy, QoS tie-break
    std::size_t best = 0;
    bool have_best = false;
    PathMetrics bestm{};

    std::size_t min_unhealthy_idx = 0;
    bool any_loaded = false;
    PathMetrics min_unhealthy_m{};

    PathMetrics m{};
    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (!cands[i].slot || !dp::load_metrics(*cands[i].slot, m)) continue;
        if (!any_loaded || m.rtt_us < min_unhealthy_m.rtt_us) {
            any_loaded = true;
            min_unhealthy_idx = i;
            min_unhealthy_m = m;
        }
        if (!m.healthy) continue;

        if (!have_best) {
            best = i;
            bestm = m;
            have_best = true;
            continue;
        }

        if (cfg_.prefer_qos_class) {
            const bool cand_matches = qos_match(m.qos_class, pkt.dscp);
            const bool best_matches = qos_match(bestm.qos_class, pkt.dscp);
            if (cand_matches && !best_matches &&
                m.rtt_us <= bestm.rtt_us + cfg_.tie_margin_us) {
                best = i;
                bestm = m;
                continue;
            }
            if (best_matches && !cand_matches &&
                bestm.rtt_us <= m.rtt_us + cfg_.tie_margin_us) {
                continue;
            }
        }

        if (m.rtt_us < bestm.rtt_us) {
            best = i;
            bestm = m;
        }
    }

    if (!have_best) {
        if (!any_loaded) {
            return RouteDecision{kInvalidPathId, SelectReason::SnapshotUnavailable};
        }
        if (!cfg_.allow_degraded_fallback) {
            return RouteDecision{kInvalidPathId, SelectReason::AllPathsUnhealthy};
        }
        return RouteDecision{cands[min_unhealthy_idx].id, SelectReason::DegradedFallback};
    }

    // Optional exploration (disabled by default)
    if (cfg_.explore_ppm) {
        XorShift32 rng{pkt.flow_hash ^ salt_.load(std::memory_order_relaxed)};
        if (rng.next_bounded(1'000'000u) < cfg_.explore_ppm) {
            const auto n = static_cast<std::uint32_t>(cands.size());
            const auto start = rng.next_bounded(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                const auto k = (start + i) % n;
                if (k != best && cands[k].slot && dp::load_metrics(*cands[k].slot, m) && m.healthy) {
                    salt_.fetch_add(0x9E37u, std::memory_order_relaxed);
                    return RouteDecision{cands[k].id, SelectReason::SelectedHealthy};
                }
            }
        }
    }
    return RouteDecision{cands[best].id, SelectReason::SelectedHealthy};
}

PathId LatencyAwarePolicy::choose(std::span<const CandidateRef> cands,
                                  const PacketContext& pkt) noexcept {
    return choose_decision(cands, pkt).path_id;
}

} // namespace alpha::routing
