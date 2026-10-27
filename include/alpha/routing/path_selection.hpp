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

/// Path identifier type (index/handle into candidate set; 0 denotes invalid/none).
using PathId = std::uint32_t;
inline constexpr PathId kInvalidPathId = 0;

/// Explicit data-plane path selection outcome reason.
enum class SelectReason : std::uint8_t {
    SelectedHealthy = 0, ///< Healthy candidate selected normally
    DegradedFallback,    ///< All candidates unhealthy; forwarded via degraded fallback policy
    NoCandidates,        ///< Candidate list was empty (no route)
    AllPathsUnhealthy,   ///< All readable candidates were unhealthy and degraded fallback is disabled
    SnapshotUnavailable  ///< Candidate metric slots or policy binding could not be read
};

/// Result of a data-plane path selection decision.
struct RouteDecision final {
    PathId       path_id{kInvalidPathId};
    SelectReason reason{SelectReason::NoCandidates};

    [[nodiscard]] constexpr bool forwarded() const noexcept {
        return reason == SelectReason::SelectedHealthy ||
               reason == SelectReason::DegradedFallback;
    }
};

/// Per-path metrics visible to data-plane policies (e.g., RTT, jitter, loss, health).
struct PathMetrics final {
    std::uint32_t rtt_us{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t one_way_delay_us{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t jitter_us{0};
    std::uint32_t loss_ppm{0};
    std::uint32_t avail_kbps{0};
    std::uint8_t  qos_class{0};
    bool          healthy{false};
};

/// Explicit alias distinguishing the data-plane metrics representation.
using DpPathMetrics = PathMetrics;

struct alignas(ALPHA_CACHELINE) MetricsSlot final {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this target");
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this target");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "std::atomic<bool> must be lock-free on this target");

    std::atomic<std::uint32_t> seq{0}; // even=stable, odd=writer active
    std::atomic<std::uint32_t> rtt_us{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<std::uint32_t> one_way_delay_us{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<std::uint32_t> jitter_us{0};
    std::atomic<std::uint32_t> loss_ppm{0};
    std::atomic<std::uint32_t> avail_kbps{0};
    std::atomic<std::uint8_t>  qos_class{0};
    std::atomic<bool>          healthy{false};
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
    explicit RoundRobinPolicy(bool allow_degraded_fallback = true) noexcept
        : allow_degraded_fallback_(allow_degraded_fallback) {}

    RouteDecision choose_decision(std::span<const CandidateRef> cands,
                                  const PacketContext& pkt) noexcept;
    PathId choose(std::span<const CandidateRef> cands,
                  const PacketContext& pkt) noexcept;
private:
    std::atomic<std::uint32_t> idx_{0};
    bool allow_degraded_fallback_{true};
};

class FlowHashPolicy final {
public:
    explicit FlowHashPolicy(bool skip_unhealthy = true,
                            bool allow_degraded_fallback = true) noexcept;

    RouteDecision choose_decision(std::span<const CandidateRef> cands,
                                  const PacketContext& pkt) noexcept;
    PathId choose(std::span<const CandidateRef> cands,
                  const PacketContext& pkt) noexcept;
private:
    bool skip_unhealthy_;
    bool allow_degraded_fallback_;
};

struct LatencyAwareConfig final {
    std::uint32_t tie_margin_us{200};
    std::uint32_t explore_ppm{0};
    bool prefer_qos_class{true};
    bool allow_degraded_fallback{true};
};

class LatencyAwarePolicy final {
public:
    explicit LatencyAwarePolicy(LatencyAwareConfig cfg = {}) noexcept;

    RouteDecision choose_decision(std::span<const CandidateRef> cands,
                                  const PacketContext& pkt) noexcept;
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

template <typename Policy>
inline RouteDecision select_path_decision(Policy& policy,
                                          std::span<const CandidateRef> cands,
                                          const PacketContext& pkt) noexcept {
    return policy.choose_decision(cands, pkt);
}

} // namespace alpha::routing

