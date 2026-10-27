#pragma once
/**
 * @file failover_policy.hpp
 * @brief Failover policy with hysteresis and optional return-to-primary behavior.
 * @details All defaults are named in constants.hpp to avoid magic numbers.
 */

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>
#include "alpha/routing/qos_policy.hpp"
#include "alpha/config/constants.hpp"

namespace alpha::routing {

/** @enum HealthState
 *  @brief High-level health classification of a path.
 */
enum class HealthState : uint8_t { Up, Degraded, Down };

/** @struct FailoverConfig
 *  @brief Configuration for failover hysteresis and return-to-primary.
 */
struct FailoverConfig {
    std::string primary_path_id;             ///< Optional preferred path identifier
    bool        return_to_primary{alpha::config::constants::FAILOVER_RETURN_TO_PRIMARY}; ///< Enable return to primary
    double      improve_pct_to_switch{alpha::config::constants::FAILOVER_IMPROVE_PCT_TO_SWITCH}; ///< Required relative improvement
    uint32_t    min_hold_ms{alpha::config::constants::FAILOVER_MIN_HOLD_MS};   ///< Dwell time before switching
    uint32_t    recovery_hold_ms{alpha::config::constants::FAILOVER_RECOVERY_HOLD_MS}; ///< Primary recovery dwell
    bool        allow_degraded{true};        ///< Permit HealthState::Degraded candidates as fallback
};

/** @struct PathHealth
 *  @brief Health state of a path and the last transition time.
 */
struct PathHealth {
    std::string path_id; ///< Path identifier
    HealthState state{HealthState::Up}; ///< Current health state
    std::chrono::steady_clock::time_point last_change{}; ///< Last state change (steady clock)
};

/** @struct FailoverDecision
 *  @brief Result of a failover evaluation.
 */
struct FailoverDecision {
    std::string next_path_id; ///< Path to switch to
    std::string reason;       ///< Human/observability reason string
};

/** @class FailoverPolicy
 *  @brief Decides whether/when to switch paths based on QoS score and health.
 */
class FailoverPolicy {
public:
    /// Construct with configuration.
    explicit FailoverPolicy(FailoverConfig cfg) noexcept : cfg_(cfg) {}

    /**
     * @brief Evaluate the need to switch from the current path.
     * @details Uses internally tracked selection-change timestamp for `min_hold_ms`
     *          and `PathHealth::last_change` for primary `recovery_hold_ms`.
     * @param current_path_id Current active path identifier.
     * @param scored_candidates Vector of QoS scores for candidate paths.
     * @param health Vector of health states corresponding to paths.
     * @param now Monotonic time for hysteresis checks.
     * @return A decision if a switch is recommended; std::nullopt to keep current.
     */
    std::optional<FailoverDecision>
    evaluate(const std::string& current_path_id,
             const std::vector<QoSScore>& scored_candidates,
             const std::vector<PathHealth>& health,
             std::chrono::steady_clock::time_point now) const;

    /**
     * @brief Evaluate failover with an explicit last-selected-path switch timestamp.
     * @param current_path_id Current active path identifier.
     * @param scored_candidates Vector of QoS scores for candidate paths.
     * @param health Vector of health states corresponding to paths.
     * @param now Monotonic time for hysteresis checks.
     * @param last_switch_time Timestamp when `current_path_id` was selected.
     * @return A decision if a switch is recommended; std::nullopt to keep current.
     */
    std::optional<FailoverDecision>
    evaluate(const std::string& current_path_id,
             const std::vector<QoSScore>& scored_candidates,
             const std::vector<PathHealth>& health,
             std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::time_point last_switch_time) const;

    /// Record an explicit path selection change at `when`.
    void note_switch(std::string path_id,
                     std::chrono::steady_clock::time_point when) noexcept {
        tracked_path_id_  = std::move(path_id);
        last_switch_time_ = when;
    }

    /// @return Timestamp of the last recorded path switch.
    [[nodiscard]] std::chrono::steady_clock::time_point last_switch_time() const noexcept {
        return last_switch_time_;
    }

    /// @return Current configuration (by const reference).
    const FailoverConfig& config() const noexcept { return cfg_; }

    /// Replace the configuration.
    void update_config(FailoverConfig c) noexcept { cfg_ = std::move(c); }

private:
    /// Lookup a path's HealthState.
    static HealthState state_of(const std::string& id, const std::vector<PathHealth>& h);

    /// Lookup a path's last health transition timestamp.
    static std::chrono::steady_clock::time_point
    last_health_change_of(const std::string& id, const std::vector<PathHealth>& h) noexcept;

    /// Check dwell/hold timers to allow switching.
    bool allow_switch(std::chrono::steady_clock::time_point since,
                      std::chrono::steady_clock::time_point now,
                      uint32_t hold_ms) const noexcept;

private:
    FailoverConfig cfg_{}; ///< Policy configuration
    mutable std::string tracked_path_id_{}; ///< Last observed/selected active path
    mutable std::chrono::steady_clock::time_point last_switch_time_{}; ///< Last path switch time
};

} // namespace alpha::routing
