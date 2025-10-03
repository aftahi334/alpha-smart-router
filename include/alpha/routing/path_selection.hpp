#pragma once


/**
 * @file path_selection.hpp
 * @brief Routing path selection interfaces and data structures (lock-free snapshots).
 * @note Readers use acquire+recheck; writers publish with release (seqlock pattern).
 */
#include <cstdint>
#include <atomic>
#include <span>
#include <limits>

#ifndef ALPHA_CACHELINE
#define ALPHA_CACHELINE 64
#endif

namespace alpha::routing {

/// Path identifier type (index into candidate set).
using PathId = std::uint32_t;

/// Per-path metrics visible to policies (e.g., RTT, health).
struct PathMetrics final {
    std::uint32_t rtt_us{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t one_way_delay_us{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t loss_ppm{0};
    std::uint32_t avail_kbps{0};
    std::uint8_t  qos_class{0};
    bool          healthy{false};
};

struct alignas(ALPHA_CACHELINE) MetricsSlot final {
    std::atomic<std::uint32_t> seq{0}; // even=stable, odd=writer active
    PathMetrics                metrics{};
};

/// Reference to a candidate path (id + pointer to metrics slot).
struct CandidateRef final {
    PathId             id{0};
    const MetricsSlot* slot{nullptr};
};

/// Minimal per-packet context used by policies.
struct PacketContext final {
    std::uint32_t flow_hash{0};
    std::uint8_t  dscp{0};
};

namespace cp {
// Control-plane: publish new metrics into a slot (single writer per slot).
void update_metrics(MetricsSlot& s, const PathMetrics& m) noexcept;
}

namespace dp {
// Data-plane: lock-free snapshot read of a slot. Returns false on rare retry fail.
bool load_metrics(const MetricsSlot& s, PathMetrics& out) noexcept;
}

// Internal QoS helper (definition in .cpp)
bool qos_match(std::uint8_t path_class, std::uint8_t dscp) noexcept;

// ---------------- Policies (declarations) ----------------

class RoundRobinPolicy final {
public:
    RoundRobinPolicy() = default;
    PathId choose(std::span<const CandidateRef> cands,
                  const PacketContext& pkt) noexcept;
private:
    std::atomic<std::uint32_t> idx_{0};
};

class FlowHashPolicy final {
public:
    explicit FlowHashPolicy(bool skip_unhealthy = true) noexcept;
    PathId choose(std::span<const CandidateRef> cands,
                  const PacketContext& pkt) noexcept;
private:
    bool skip_unhealthy_;
};

struct LatencyAwareConfig final {
    std::uint32_t tie_margin_us{200};
    std::uint32_t explore_ppm{0};
    bool prefer_qos_class{true};
};

class LatencyAwarePolicy final {
public:
    explicit LatencyAwarePolicy(LatencyAwareConfig cfg = {}) noexcept;
    PathId choose(std::span<const CandidateRef> cands,
                  const PacketContext& pkt) noexcept;
private:
    LatencyAwareConfig cfg_{};
    std::atomic<std::uint32_t> salt_{0xA5A55A5Au};
};

// Helper for compile-time policy binding

template <typename Policy>
/// Hot-path entry: choose a path via current policy binding (no locks).
inline PathId select_path(Policy& policy,
                          std::span<const CandidateRef> cands,
                          const PacketContext& pkt) noexcept {
    return policy.choose(cands, pkt);
}

} // namespace alpha::routing

