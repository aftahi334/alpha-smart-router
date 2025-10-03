#pragma once
/**
 * @file rt_profile.hpp
 * @brief Central defaults for *real-time* priorities used by dataplane threads.
 *
 * Intent:
 *  - Keep **mechanism** in alpha::os::rt (affinity + policy application).
 *  - Keep **policy** (which priorities to use) here, so apps can override without
 *    touching OS helpers.
 *
 * Guidance:
 *  - Values are **mid-band** to leave headroom for exceptional tasks.
 *  - Linux SCHED_{FIFO,RR} typical range: [1..99]. QNX allows wider ranges.
 *  - Prefer SCHED_RR when multiple threads share the same priority to reduce starvation.
 *
 * Example:
 *   using alpha::os::RtSchedPolicy;
 *   using alpha::os::prio;
 *   alpha::os::bind_and_prioritize({ .cpu = 2,
 *                                    .policy = RtSchedPolicy::Fifo,
 *                                    .priority = prio::kRX });
 */
#include "alpha/os/rt.hpp"

namespace alpha::os::prio {

    /// @brief General-purpose RT work (telemetry, soft real-time tasks).
    inline constexpr int kDefault  = 50;

    /// @brief Ingress worker (RX) priority — preempts kDefault.
    inline constexpr int kRX       = 60;

    /// @brief Egress worker (TX) priority — slightly above RX to drain queues promptly.
    inline constexpr int kTX       = 70;

    /// @brief Critical short-lived tasks (watchdog/emergency). Use sparingly.
    inline constexpr int kWatchdog = 80;

} // namespace alpha::os::prio
