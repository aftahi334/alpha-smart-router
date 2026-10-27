#include "alpha/routing/policy_binding.hpp"

namespace alpha::routing {

    void cp::clear_policy(PolicyBinding& b) noexcept {
        const auto start = b.seq.load(std::memory_order_relaxed);
        const auto odd   = start | 1u;
        // Writer enter (odd): make binding unreachable to readers before clearing fields.
        b.seq.store(odd, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        b.fn.store(nullptr, std::memory_order_relaxed);
        b.state.store(nullptr, std::memory_order_relaxed);
        // Publish even seq with release; readers that see even also see fn/state.
        b.seq.store(odd + 1u, std::memory_order_release);
    }

    bool dp::snapshot_binding(const PolicyBinding& b, ChooseFn& out_fn, void*& out_state) noexcept {
        for (int i = 0; i < 4; ++i) {
            // Acquire pairs with publisher's release; even => candidate stable snapshot.
            const auto s1 = b.seq.load(std::memory_order_acquire);
            if ((s1 & 1u) != 0u) continue;
            const auto fn = b.fn.load(std::memory_order_relaxed);
            const auto st = b.state.load(std::memory_order_relaxed);
            // Ensure relaxed loads complete before rechecking seq.
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto s2 = b.seq.load(std::memory_order_relaxed);
            if (s1 == s2) { out_fn = fn; out_state = st; return fn && st; }
        }
        out_fn=nullptr; out_state=nullptr; return false;
    }

    PathId dp::select_path(const PolicyBinding& b,
                           std::span<const CandidateRef> cands,
                           const PacketContext& pkt) noexcept {
        ChooseFn fn{}; void* st{};
        if (!snapshot_binding(b, fn, st)) return 0;
        return fn(st, cands, pkt);
    }

} // namespace alpha::routing

