/**
* @file config_loader.cpp
 * @brief Stub loader that returns named defaults; parser TBD.
 */
#include "alpha/config/config_loader.hpp"
#include "alpha/config/constants.hpp"

namespace alpha::config {
    using namespace alpha::routing;
    using namespace alpha::config::constants;

    static QoSConfig default_qos() {
        QoSConfig cfg;
        cfg.thresholds_by_class = {
            {QoSClass::Bulk,        {QOS_BULK_MAX_LAT_US, QOS_BULK_MAX_JITTER_US, QOS_BULK_MAX_LOSS}},
            {QoSClass::BestEffort,  {QOS_BE_MAX_LAT_US,   QOS_BE_MAX_JITTER_US,  QOS_BE_MAX_LOSS}},
            {QoSClass::Interactive, {QOS_INT_MAX_LAT_US,  QOS_INT_MAX_JITTER_US, QOS_INT_MAX_LOSS}},
            {QoSClass::Realtime,    {QOS_RT_MAX_LAT_US,   QOS_RT_MAX_JITTER_US,  QOS_RT_MAX_LOSS}}
        };
        cfg.weights = { .latency = QOS_WEIGHT_LATENCY, .jitter = QOS_WEIGHT_JITTER, .loss = QOS_WEIGHT_LOSS };
        cfg.dscp_by_class = {
            {QoSClass::Bulk,        DSCP_CS1},
            {QoSClass::BestEffort,  DSCP_BE},
            {QoSClass::Interactive, DSCP_AF31},
            {QoSClass::Realtime,    DSCP_EF}
        };
        return cfg;
    }

    RouterConfig Loader::load_from_file(const std::string&) {
        RouterConfig rc;
        rc.qos = default_qos();
        rc.failover = FailoverConfig{}; // picks defaults from constants
        rc.ingress  = IngressConfig{};  // seed/strategy defaults from constants
        // TODO: parse TOML/JSON (toml++ / nlohmann::json) and fill fields.
        return rc;
    }

} // namespace alpha::config