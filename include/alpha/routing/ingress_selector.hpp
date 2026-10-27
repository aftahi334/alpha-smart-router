#pragma once
/**
 * @file ingress_selector.hpp
 * @brief Ingress selection: PolicyDeterministic (RR/hash) and RouteInformed (BGP oracle).
 * @details Default seed is named in constants; override via config to avoid magic numbers.
 */

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "alpha/config/constants.hpp"
#include "alpha/routing/pop.hpp"

namespace alpha::routing {
/**
 * @enum IngressStrategy
 * @brief Local deterministic strategies for selecting an ingress PoP.
 */
enum class IngressStrategy : uint8_t {
    RoundRobin,   ///< Monotonic RR over the configured PoP list
    HashSourceIP, ///< Hash source IP into PoP index (falls back to RR if absent/invalid)
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

/**
 * @struct Flow5Tuple
 * @brief Compact 5-tuple flow key for deterministic data-plane ingress selection.
 *
 * `src_ip` and `dst_ip` use canonical host-order IPv4 values (e.g. 192.0.2.1 -> 0xC0000201u),
 * matching `IngressSelector::hash_ip_literal` for IPv4 literals.
 */
struct Flow5Tuple {
    uint32_t src_ip{0};    ///< Source IPv4 address
    uint32_t dst_ip{0};    ///< Destination IPv4 address
    uint16_t src_port{0};  ///< Source L4 port
    uint16_t dst_port{0};  ///< Destination L4 port
    uint8_t  proto{0};     ///< IP protocol (e.g., 6 = TCP, 17 = UDP)

    bool operator==(const Flow5Tuple&) const = default;
};

class BgpOracle; // forward decl

/**
 * @class IngressSelector
 * @brief Selector that supports both PolicyDeterministic and RouteInformed.
 *
 * PoPs with `Health::Down` are excluded from selection.
 * Under `HashSourceIP` or `Hash5Tuple`, if a caller invokes the unkeyed overload
 * (`chooseIngress(serviceId)`) or supplies an empty/invalid IP literal, selection
 * falls back to `RoundRobin` so unkeyed traffic still distributes across eligible PoPs.
 */
class IngressSelector {
public:
    /// Load/replace the set of available PoPs (filters out Health::Down PoPs).
    void loadPops(const PopList& pops);

    /**
     * @brief Choose ingress without client IP or flow key.
     * @details In RouteInformed mode, consults the attached BGP oracle first.
     *          In PolicyDeterministic mode (or if the oracle has no route),
     *          uses RoundRobin because no client/flow hash input is provided.
     * @param serviceId Logical service (e.g., anycast label).
     */
    std::string chooseIngress(const std::string& serviceId) const;

    /**
     * @brief Choose ingress with client IP (enables client-aware oracle/hash).
     * @details In PolicyDeterministic mode with HashSourceIP or Hash5Tuple,
     *          hashes `clientSrcIp` (IPv4 or IPv6 literal) with `cfg_.seed`.
     *          Falls back to RoundRobin if `clientSrcIp` is empty or invalid.
     * @param serviceId Logical service (e.g., anycast label).
     * @param clientSrcIp Client IPv4/IPv6 string.
     */
    std::string chooseIngress(const std::string& serviceId,
                              const std::string& clientSrcIp) const;

    /**
     * @brief Choose ingress with a 5-tuple flow key.
     * @details In PolicyDeterministic mode:
     *          - RoundRobin: monotonic round-robin over eligible PoPs.
     *          - HashSourceIP: hashes `flow.src_ip` with `cfg_.seed`.
     *          - Hash5Tuple: hashes all five fields of `flow` with `cfg_.seed`.
     */
    std::string chooseIngress(const std::string& serviceId,
                              const Flow5Tuple& flow) const;

    /// Update configuration
    void update_config(IngressConfig c) noexcept { cfg_ = c; }
    /// Attach BGP oracle (FRR-backed or simulator) for RouteInformed mode
    void attachOracle(std::shared_ptr<BgpOracle> oracle) { oracle_ = std::move(oracle); }

    /// 64-bit avalanche hash used by hashing strategies
    static uint64_t mix(uint64_t x, uint64_t seed) noexcept;

    /// Hash a 32-bit IPv4 address (host-order 0xAABBCCDD) with seed
    static uint64_t hash_ipv4(uint32_t ipv4_addr, uint64_t seed) noexcept;

    /// Hash a 5-tuple flow key with seed
    static uint64_t hash_five_tuple(const Flow5Tuple& flow, uint64_t seed) noexcept;

    /// Parse and hash an IPv4 or IPv6 literal string; returns nullopt if invalid
    static std::optional<uint64_t> hash_ip_literal(std::string_view ip_literal,
                                                   uint64_t seed) noexcept;

private:
    /// Deterministic local policy path given an optional pre-mixed hash value
    std::string choose_policy_deterministic(const std::vector<std::string>& ids,
                                            std::optional<uint64_t> mixed_hash = std::nullopt) const;

    /// Check whether a PoP ID is eligible (not marked Health::Down in pops_)
    bool is_pop_eligible(std::string_view pop_id) const noexcept;

private:
    IngressConfig cfg_{};                      ///< Current configuration
    PopList pops_;                             ///< Configured PoPs
    std::vector<std::string> eligible_ids_;    ///< Cached IDs with Health != Down
    std::shared_ptr<BgpOracle> oracle_;        ///< Oracle for RouteInformed
    mutable std::atomic<uint64_t> rr_{0};      ///< Lock-free RR counter
};

} // namespace alpha::routing
