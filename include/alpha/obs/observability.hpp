#pragma once
/**
 * @file observability.hpp
 * @brief Minimal observability facade: decision events + counters.
 * @details Replace the backing implementation with spdlog/OpenTelemetry later.
 */

#include <string>
#include <vector>
#include <cstdint>
#include "alpha/routing/qos_policy.hpp"

namespace alpha::obs {

    /** @struct Counters
     *  @brief Process-level counters for routing decisions.
     */
    struct Counters {
        uint64_t decisions{0};          ///< Total decisions recorded
        uint64_t failover_triggers{0};  ///< Times a failover was triggered
        uint64_t degraded_choices{0};   ///< Decisions that chose a non-compliant path
    };

    /** @struct DecisionEvent
     *  @brief Payload describing a single routing decision.
     */
    struct DecisionEvent {
        std::string decision_id;         ///< Caller-provided UUID/monotonic id
        std::string selected_path;       ///< Chosen path identifier
        alpha::routing::QoSClass clazz;  ///< Traffic class
        double      best_score{0.0};     ///< Score of the selected path
        bool        strict_mode{false};  ///< Whether threshold compliance was enforced
        std::vector<alpha::routing::QoSScore> scored; ///< Scores for all candidates
        std::string reason;              ///< Reason label (for humans/logs)
    };

    /** @class Observer
     *  @brief Observability sink interface.
     */
    class Observer {
    public:
        virtual ~Observer() = default;
        /// Record a single decision event.
        virtual void record(const DecisionEvent& e) = 0;
        /// Return a snapshot of counters.
        virtual Counters snapshot() const = 0;
    };

    // Optional factory declaration (implemented in .cpp)
    Observer* make_simple_observer();

} // namespace alpha::obs
