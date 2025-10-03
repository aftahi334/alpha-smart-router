#pragma once
/**
 * @file constants.hpp
 * @brief Centralized named defaults for control‑plane components.
 * @details These values eliminate magic numbers from the codebase. Override via the
 *          Config Loader (TOML/JSON) in production deployments.
 */

#include <cstdint>

namespace alpha::config::constants {

// =====================
// DSCP PHB Codepoints (6-bit in IPv4 TOS / IPv6 Traffic Class)
// RFC 2474/2597/3246 etc. Keep as names (not literals) to avoid magic numbers.
// =====================
/// Best Effort: 000000
inline constexpr uint8_t DSCP_BE   = 0x00;
/// Class Selector 1: 001000
inline constexpr uint8_t DSCP_CS1  = 0x08;
/// Assured Forwarding 31: 101000
inline constexpr uint8_t DSCP_AF31 = 0x28;
/// Expedited Forwarding: 101110
inline constexpr uint8_t DSCP_EF   = 0x2E;

// =====================
// QoS Default Thresholds (targets used for normalization)
// Units: microseconds for latency/jitter; fraction (0.0–1.0) for loss
// =====================
inline constexpr uint32_t QOS_BULK_MAX_LAT_US        = 20000; ///< 20 ms
inline constexpr uint32_t QOS_BULK_MAX_JITTER_US     = 10000; ///< 10 ms
inline constexpr double   QOS_BULK_MAX_LOSS          = 0.05;  ///< 5%

inline constexpr uint32_t QOS_BE_MAX_LAT_US          = 15000; ///< 15 ms
inline constexpr uint32_t QOS_BE_MAX_JITTER_US       =  8000; ///< 8 ms
inline constexpr double   QOS_BE_MAX_LOSS            = 0.02;  ///< 2%

inline constexpr uint32_t QOS_INT_MAX_LAT_US         =  8000; ///< 8 ms
inline constexpr uint32_t QOS_INT_MAX_JITTER_US      =  3000; ///< 3 ms
inline constexpr double   QOS_INT_MAX_LOSS           = 0.01;  ///< 1%

inline constexpr uint32_t QOS_RT_MAX_LAT_US          =  4000; ///< 4 ms
inline constexpr uint32_t QOS_RT_MAX_JITTER_US       =  1500; ///< 1.5 ms
inline constexpr double   QOS_RT_MAX_LOSS            = 0.005; ///< 0.5%

// =====================
// QoS Weighting Defaults (relative importance during blending)
// =====================
inline constexpr double QOS_WEIGHT_LATENCY = 0.6;  ///< Weight of latency factor
inline constexpr double QOS_WEIGHT_JITTER  = 0.3;  ///< Weight of jitter factor
inline constexpr double QOS_WEIGHT_LOSS    = 0.1;  ///< Weight of packet loss factor

// =====================
// Failover Defaults
// =====================
inline constexpr bool     FAILOVER_RETURN_TO_PRIMARY      = true;   ///< Prefer returning to primary when healthy
inline constexpr double   FAILOVER_IMPROVE_PCT_TO_SWITCH  = 0.10;   ///< Require +10% score improvement to switch
inline constexpr uint32_t FAILOVER_MIN_HOLD_MS            = 3000;   ///< Dwell to prevent flapping
inline constexpr uint32_t FAILOVER_RECOVERY_HOLD_MS       = 5000;   ///< Time primary must remain healthy before R2P

// =====================
// Ingress Selector Defaults
// =====================
inline constexpr uint64_t INGRESS_HASH_SEED_DEFAULT = 0xA17A5EEDULL; ///< Deterministic hash salt

// =====================
// Anycast+BGP Simulator Defaults (attributes used if not provided)
// =====================
inline constexpr uint32_t BGP_SIM_DEFAULT_LOCAL_PREF = 100; ///< Higher is better
inline constexpr uint32_t BGP_SIM_DEFAULT_AS_PATH    = 2;   ///< Shorter is better
inline constexpr uint32_t BGP_SIM_DEFAULT_MED        = 100; ///< Lower is better
inline constexpr uint32_t BGP_SIM_DEFAULT_IGP_COST   = 100; ///< Lower is better

} // namespace alpha::config::constants
