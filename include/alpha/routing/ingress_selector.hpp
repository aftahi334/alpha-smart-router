#pragma once
/**
 * @file ingress_selector.hpp
 * @brief Ingress selection: PolicyDeterministic (RR/hash) and RouteInformed (BGP oracle).
 * @details Default seed is named in constants; override via config to avoid magic numbers.
 */

#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <cstdint>
#include "alpha/config/constants.hpp"
#include "alpha/routing/pop.hpp"

namespace alpha::routing {
/**
 * @enum IngressStrategy
 * @brief Local deterministic strategies for selecting an ingress PoP.
 */
enum class IngressStrategy : uint8_t {
    RoundRobin,   ///< Monotonic RR over the configured PoP list
    HashSourceIP, ///< Hash 32-bit source IP into PoP index
    Hash5Tuple    ///< Hash 5-tuple (src,dst,srcPort,dstPort,proto) into PoP index
};

/**
 * @enum IngressMode
 * @brief Top-level mode: app-layer policy vs route-informed anycast best-path.
 */
enum class IngressMode : uint8_t {
    PolicyDeterministic = 0, ///< App-layer RR/hash; no BGP consult
    RouteInformed           ///< BGP/anycast best-path via oracle
};

/**
 * @struct IngressConfig
 * @brief Config for ingress selection (mode, strategy, seed).
 */
struct IngressConfig {
    IngressMode     mode{IngressMode::PolicyDeterministic}; ///< High-level mode
    IngressStrategy strategy{IngressStrategy::RoundRobin};  ///< Local strategy
    uint64_t        seed{alpha::config::constants::INGRESS_HASH_SEED_DEFAULT}; ///< Salt for hashing strategies
};

class BgpOracle; // forward decl

/**
 * @class IngressSelector
 * @brief Selector that supports both PolicyDeterministic and RouteInformed.
 */
class IngressSelector {
public:
    /// Load/replace the set of available PoPs
    void loadPops(const PopList& pops);

    /**
     * @brief Choose ingress without client IP (best effort).
     * @param serviceId Logical service (e.g., anycast label).
     */
    std::string chooseIngress(const std::string& serviceId) const;

    /**
     * @brief Choose ingress with client IP (enables client-aware oracle/hash).
     * @param serviceId Logical service (e.g., anycast label).
     * @param clientSrcIp Client IPv4/IPv6 string (optional for policy).
     */
    std::string chooseIngress(const std::string& serviceId,
                              const std::string& clientSrcIp) const;

    /// Update configuration
    void update_config(IngressConfig c) noexcept { cfg_ = c; }
    /// Attach BGP oracle (FRR-backed or simulator) for RouteInformed mode
    void attachOracle(std::shared_ptr<BgpOracle> oracle) { oracle_ = std::move(oracle); }

private:
    /// 64-bit avalanche hash used by hashing strategies
    static uint64_t mix(uint64_t x, uint64_t seed) noexcept;

    /// Deterministic local policy path
    std::string choose_policy_deterministic(const std::vector<std::string>& ids,
                                            uint64_t flow_hash = 0) const noexcept;

private:
    IngressConfig cfg_{};                      ///< Current configuration
    PopList pops_;                    ///< Available PoPs
    std::shared_ptr<BgpOracle> oracle_;        ///< Oracle for RouteInformed
    mutable std::atomic<uint64_t> rr_{0};      ///< Lock-free RR counter
};

} // namespace alpha::routing
