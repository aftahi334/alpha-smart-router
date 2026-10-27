#pragma once


/**
 * @file policy_binding.hpp
 * @brief Dynamic control-plane binding of routing policies for the data plane.
 * @note Writers publish (release) an even seq; readers retry on odd/changed seq (seqlock).
 */
#include <atomic>
#include <span>
#include <cstdint>
#include "alpha/routing/path_selection.hpp"

#ifndef ALPHA_CACHELINE
#define ALPHA_CACHELINE 64
#endif

namespace alpha::routing {

/// Policy function signature used by the data plane (fn(state, cands, pkt)).
using ChooseFn = PathId(*)(void* state,
                           std::span<const CandidateRef> cands,
                           const PacketContext& pkt) noexcept;

/// Policy decision function signature returning path_id + explicit SelectReason.
using ChooseDecisionFn = RouteDecision(*)(void* state,
                                          std::span<const CandidateRef> cands,
                                          const PacketContext& pkt) noexcept;

namespace detail {
    template <typename Policy>
    inline PathId choose_thunk(void* state,
                               std::span<const CandidateRef> cands,
                               const PacketContext& pkt) noexcept {
        return static_cast<Policy*>(state)->choose(cands, pkt);
    }

    template <typename Policy>
    inline RouteDecision choose_decision_thunk(void* state,
                                               std::span<const CandidateRef> cands,
                                               const PacketContext& pkt) noexcept {
        return static_cast<Policy*>(state)->choose_decision(cands, pkt);
    }
}

struct alignas(ALPHA_CACHELINE) PolicyBinding final {
    std::atomic<std::uint32_t>    seq{0};
    std::atomic<ChooseFn>         fn{nullptr};
    std::atomic<ChooseDecisionFn> decision_fn{nullptr};
    std::atomic<void*>            state{nullptr};
};

/// Control plane operations (publish/clear policies).
namespace cp {
    template <typename Policy>
    inline void publish_policy(PolicyBinding& b, Policy& policy) noexcept {
        const auto start = b.seq.load(std::memory_order_relaxed);
        const auto odd   = start | 1u;
        b.seq.store(odd, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        b.state.store(static_cast<void*>(&policy), std::memory_order_relaxed);
        b.fn.store(&detail::choose_thunk<Policy>, std::memory_order_relaxed);
        b.decision_fn.store(&detail::choose_decision_thunk<Policy>, std::memory_order_relaxed);
        b.seq.store(odd + 1u, std::memory_order_release);
    }
    void clear_policy(PolicyBinding& b) noexcept;
}

/// Data plane operations (snapshot binding, select path).
namespace dp {
    bool snapshot_binding(const PolicyBinding& b, ChooseFn& out_fn, void*& out_state) noexcept;
    bool snapshot_decision_binding(const PolicyBinding& b,
                                   ChooseDecisionFn& out_fn,
                                   void*& out_state) noexcept;

/// Resolve current policy and choose a path (hot path, no locks).
    PathId select_path(const PolicyBinding& b,
                       std::span<const CandidateRef> cands,
                       const PacketContext& pkt) noexcept;

/// Resolve current policy and return explicit RouteDecision (path_id + SelectReason).
    RouteDecision select_path_decision(const PolicyBinding& b,
                                       std::span<const CandidateRef> cands,
                                       const PacketContext& pkt) noexcept;

/// Lightweight view for threads: choose() calls select_path().
    struct WorkerPolicyView final {
        const PolicyBinding* binding{nullptr};
        inline PathId choose(std::span<const CandidateRef> c, const PacketContext& p) const noexcept {
            return binding ? select_path(*binding, c, p) : kInvalidPathId;
        }
        inline RouteDecision choose_decision(std::span<const CandidateRef> c,
                                             const PacketContext& p) const noexcept {
            if (!binding) {
                return RouteDecision{kInvalidPathId, SelectReason::SnapshotUnavailable};
            }
            return select_path_decision(*binding, c, p);
        }
    };
}

} // namespace alpha::routing

