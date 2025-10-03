/**
* @file bgp_oracle_sim.cpp
 * @brief Implementation of the anycast+BGP simulator oracle.
 */
#include "alpha/routing/bgp_oracle_sim.hpp"
#include <algorithm>

namespace alpha::routing {

    std::optional<std::string>
    SimulatedBgpOracle::serving_pop(const std::string& serviceId, const std::string&) const {
        auto it = routes_.find(serviceId);
        if (it == routes_.end() || it->second.empty()) return std::nullopt;

        const auto& v = it->second;
        // Tie-breaker: local-pref DESC, as-path ASC, MED ASC, IGP ASC, then lexicographic pop_id
        const auto* best = &v.front();
        for (const auto& r : v) {
            if (r.local_pref > best->local_pref) { best = &r; continue; }
            if (r.local_pref < best->local_pref) { continue; }
            if (r.as_path_len < best->as_path_len) { best = &r; continue; }
            if (r.as_path_len > best->as_path_len) { continue; }
            if (r.med < best->med) { best = &r; continue; }
            if (r.med > best->med) { continue; }
            if (r.igp_cost < best->igp_cost) { best = &r; continue; }
            if (r.igp_cost > best->igp_cost) { continue; }
            if (r.pop_id < best->pop_id) { best = &r; }
        }
        return best->pop_id;
    }

} // namespace alpha::routing
