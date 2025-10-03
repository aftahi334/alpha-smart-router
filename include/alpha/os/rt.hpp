#pragma once
/**
 * @file rt.hpp
 * @brief Minimal helpers to apply RT affinity/scheduling to the *current thread*.
 * @note Linux implemented (affinity + FIFO/RR). QNX implements FIFO/RR; affinity TODO.
 */

#include <cstdint>

namespace alpha::os {

    /// @brief Real-time scheduling policy.
    /// - Fifo: fixed-priority, run-to-block.
    /// - RoundRobin: fixed-priority, time-sliced among equal priorities.
    enum class RtSchedPolicy : std::uint8_t {
        Fifo = 0,
        RoundRobin = 1
    };

    /// @brief RT configuration for the current thread.
    /// @var cpu -1 to skip pinning; otherwise CPU index to pin to.
    /// @var policy Desired RT policy (FIFO/RR).
    /// @var priority RT priority (Linux typically [1..99]; QNX allows wider ranges).
    /// @note No default is provided for `priority` to avoid magic numbers. Choose explicitly
    ///       in app code (e.g., via alpha::os::prio constants from rt_profile.hpp).
    struct RtConfig {
        int cpu = -1;
        RtSchedPolicy policy = RtSchedPolicy::Fifo;
        int priority; // must be set by caller
    };

    /// @brief Apply CPU affinity (optional) and RT policy/priority to the current thread.
    /// @return true on success for this platform; false if unsupported or insufficient privileges.
    bool bind_and_prioritize(const RtConfig&);

} // namespace alpha::os
