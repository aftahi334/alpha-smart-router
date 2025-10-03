#pragma once
/**
 * @file bgp_oracle.hpp
 * @brief Pluggable oracle to answer: which PoP serves an anycast service?
 * @details Used by IngressMode::RouteInformed (FRR integration later; simulator now).
 */

#include <string>
#include <optional>

namespace alpha::routing {

    class BgpOracle {
    public:
        virtual ~BgpOracle() = default;

        /**
         * @brief Return the PoP ID that would serve 'serviceId' for a client.
         * @param serviceId Logical service (anycast label/prefix).
         * @param clientSrcIp Client IP (optional; empty for best overall).
         */
        virtual std::optional<std::string>
        serving_pop(const std::string& serviceId, const std::string& clientSrcIp = {}) const = 0;
    };

} // namespace alpha::routing
