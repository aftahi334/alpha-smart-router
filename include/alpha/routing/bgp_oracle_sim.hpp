#pragma once
/**
 * @file bgp_oracle_sim.hpp
 * @brief Lightweight Anycast+BGP simulator with a sane tie-breaker order.
 * @details Great until FRR is hooked up.
 */

#include "alpha/routing/bgp_oracle.hpp"
#include "alpha/config/constants.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>

namespace alpha::routing {

    /**
     * @struct SimRoute
     * @brief Simulated BGP route candidate to a given service (anycast prefix).
     */
    struct SimRoute {
        std::string pop_id;       ///< Candidate PoP
        uint32_t    local_pref{alpha::config::constants::BGP_SIM_DEFAULT_LOCAL_PREF}; ///< Higher wins
        uint32_t    as_path_len{alpha::config::constants::BGP_SIM_DEFAULT_AS_PATH};   ///< Lower wins
        uint32_t    med{alpha::config::constants::BGP_SIM_DEFAULT_MED};               ///< Lower wins
        uint32_t    igp_cost{alpha::config::constants::BGP_SIM_DEFAULT_IGP_COST};     ///< Lower wins
    };

    /// Per serviceId: candidates with attributes
    using SimRouteMap = std::unordered_map<std::string, std::vector<SimRoute>>;

    /**
     * @class SimulatedBgpOracle
     * @brief RouteInformed oracle backed by static/simulated BGP attributes.
     */
    class SimulatedBgpOracle final : public BgpOracle {
    public:
        /// Replace route table for the simulator
        void load_routes(SimRouteMap routes) { routes_ = std::move(routes); }

        std::optional<std::string>
        serving_pop(const std::string& serviceId, const std::string& clientSrcIp = {}) const override;

    private:
        SimRouteMap routes_;
    };

} // namespace alpha::routing
