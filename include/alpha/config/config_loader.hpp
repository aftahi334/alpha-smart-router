#pragma once
/**
 * @file config_loader.hpp
 * @brief Loader facade: return defaults now, parse TOML/JSON later.
 * @details All defaults reference named constants to avoid magic numbers.
 */

#include <string>
#include "alpha/routing/qos_policy.hpp"
#include "alpha/routing/failover_policy.hpp"
#include "alpha/routing/ingress_selector.hpp"

namespace alpha::config {

    /** @struct RouterConfig
     *  @brief Aggregate of sub-configs required by the control plane.
     */
    struct RouterConfig {
        alpha::routing::QoSConfig      qos;      ///< QoS thresholds/weights/DSCP
        alpha::routing::FailoverConfig failover; ///< Failover hysteresis/R2P
        alpha::routing::IngressConfig  ingress;  ///< Ingress policy config (mode/strategy/seed)
    };

    /** @class Loader
     *  @brief Source of router configuration (defaults or parsed files).
     */
    class Loader {
    public:
        /**
         * @brief Load configuration from a path or return defaults.
         * @param path Suggested file path (may be ignored in default stub).
         * @return RouterConfig with populated sub-configs.
         */
        static RouterConfig load_from_file(const std::string& path);
    };

} // namespace alpha::config
