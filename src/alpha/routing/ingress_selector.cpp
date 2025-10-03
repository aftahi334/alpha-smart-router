/**
 * @file ingress_selector.cpp
 * @brief Implementation for PolicyDeterministic and RouteInformed ingress.
 */
#include "alpha/routing/ingress_selector.hpp"
#include "alpha/routing/bgp_oracle.hpp"

namespace alpha::routing {

uint64_t IngressSelector::mix(uint64_t x, uint64_t seed) noexcept {
    // Named constants (splitmix64/wyhash-style avalanching)
    constexpr uint64_t PHI = 0x9e3779b97f4a7c15ULL;        // golden ratio constant
    constexpr uint64_t M1  = 0xff51afd7ed558ccdULL;        // mix multiplier 1
    constexpr uint64_t M2  = 0xc4ceb9fe1a85ec53ULL;        // mix multiplier 2

    x ^= seed + PHI + (x << 6) + (x >> 2);
    x ^= (x >> 33); x *= M1;
    x ^= (x >> 33); x *= M2;
    x ^= (x >> 33);
    return x;
}

void IngressSelector::loadPops(const PopList& pops) {
    pops_ = pops;
}

std::string IngressSelector::choose_policy_deterministic(const std::vector<std::string>& ids,
                                                         uint64_t flow_hash) const noexcept {
    if (ids.empty()) return {};
    switch (cfg_.strategy) {
        case IngressStrategy::RoundRobin: {
            const auto idx = rr_.fetch_add(1, std::memory_order_relaxed) % ids.size();
            return ids[static_cast<size_t>(idx)];
        }
        case IngressStrategy::HashSourceIP:
        case IngressStrategy::Hash5Tuple: {
            const auto h = mix(flow_hash, cfg_.seed);
            return ids[static_cast<size_t>(h % ids.size())];
        }
    }
    return ids.front();
}

std::string IngressSelector::chooseIngress(const std::string& serviceId) const {
    (void)serviceId; // not used by local policy; used by oracle in RouteInformed

    // RouteInformed: Ask the BGP oracle which PoP actually serves the request.
    if (cfg_.mode == IngressMode::RouteInformed && oracle_) {
        if (auto pop = oracle_->serving_pop(serviceId)) return *pop;
    }

    // PolicyDeterministic path: default to RR with no flow hash.
    std::vector<std::string> ids; ids.reserve(pops_.size());
    for (const auto& p : pops_) ids.push_back(p.id);
    return choose_policy_deterministic(ids, /*flow_hash=*/0);
}

std::string IngressSelector::chooseIngress(const std::string& serviceId,
                                           const std::string& clientSrcIp) const {
    // RouteInformed can be client-aware
    if (cfg_.mode == IngressMode::RouteInformed && oracle_) {
        if (auto pop = oracle_->serving_pop(serviceId, clientSrcIp)) return *pop;
    }
    // PolicyDeterministic with hash strategy could use client-derived flow hash here later.
    return chooseIngress(serviceId);
}

} // namespace alpha::routing
