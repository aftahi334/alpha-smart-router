/**
 * @file ingress_selector.cpp
 * @brief Implementation for PolicyDeterministic and RouteInformed ingress.
 */
#include "alpha/routing/ingress_selector.hpp"
#include "alpha/routing/bgp_oracle.hpp"

#include <arpa/inet.h>
#include <cstring>

namespace alpha::routing {

namespace {
constexpr std::size_t kMaxIpLiteralLen = 64;
} // namespace

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

uint64_t IngressSelector::hash_ipv4(uint32_t ipv4_addr, uint64_t seed) noexcept {
    return mix(static_cast<uint64_t>(ipv4_addr), seed);
}

uint64_t IngressSelector::hash_five_tuple(const Flow5Tuple& flow, uint64_t seed) noexcept {
    const uint64_t word0 = (static_cast<uint64_t>(flow.src_ip) << 32) |
                           static_cast<uint64_t>(flow.dst_ip);
    const uint64_t word1 = (static_cast<uint64_t>(flow.src_port) << 32) |
                           (static_cast<uint64_t>(flow.dst_port) << 16) |
                           static_cast<uint64_t>(flow.proto);
    return mix(word1, mix(word0, seed));
}

std::optional<uint64_t> IngressSelector::hash_ip_literal(std::string_view ip_literal,
                                                         uint64_t seed) noexcept {
    if (ip_literal.empty() || ip_literal.size() > kMaxIpLiteralLen) {
        return std::nullopt;
    }
    if (ip_literal.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }

    char ip_cstr[kMaxIpLiteralLen + 1]{};
    std::memcpy(ip_cstr, ip_literal.data(), ip_literal.size());
    ip_cstr[ip_literal.size()] = '\0';

    unsigned char v4_bytes[4]{};
    if (inet_pton(AF_INET, ip_cstr, v4_bytes) == 1) {
        const uint32_t addr = (static_cast<uint32_t>(v4_bytes[0]) << 24) |
                              (static_cast<uint32_t>(v4_bytes[1]) << 16) |
                              (static_cast<uint32_t>(v4_bytes[2]) << 8)  |
                              static_cast<uint32_t>(v4_bytes[3]);
        return hash_ipv4(addr, seed);
    }

    unsigned char v6_bytes[16]{};
    if (inet_pton(AF_INET6, ip_cstr, v6_bytes) == 1) {
        uint64_t hi = 0;
        uint64_t lo = 0;
        for (int i = 0; i < 8; ++i) {
            hi = (hi << 8) | static_cast<uint64_t>(v6_bytes[i]);
            lo = (lo << 8) | static_cast<uint64_t>(v6_bytes[i + 8]);
        }
        return mix(lo, mix(hi, seed));
    }

    return std::nullopt;
}

void IngressSelector::loadPops(const PopList& pops) {
    pops_ = pops;
    eligible_ids_.clear();
    eligible_ids_.reserve(pops_.size());
    for (const auto& p : pops_) {
        if (p.health != Health::Down && !p.id.empty()) {
            eligible_ids_.push_back(p.id);
        }
    }
}

bool IngressSelector::is_pop_eligible(std::string_view pop_id) const noexcept {
    if (pops_.empty()) {
        // When no local PoP list is loaded, trust the oracle's view.
        return !pop_id.empty();
    }
    for (const auto& p : pops_) {
        if (p.id == pop_id) {
            return p.health != Health::Down;
        }
    }
    return true;
}

std::string IngressSelector::choose_policy_deterministic(
    const std::vector<std::string>& ids,
    std::optional<uint64_t> mixed_hash) const {
    if (ids.empty()) return {};

    switch (cfg_.strategy) {
        case IngressStrategy::RoundRobin: {
            const auto idx = rr_.fetch_add(1, std::memory_order_relaxed) % ids.size();
            return ids[static_cast<size_t>(idx)];
        }
        case IngressStrategy::HashSourceIP:
        case IngressStrategy::Hash5Tuple: {
            if (mixed_hash.has_value()) {
                return ids[static_cast<size_t>(*mixed_hash % ids.size())];
            }
            // Fallback when hash input is absent or invalid: distribute via RR.
            const auto idx = rr_.fetch_add(1, std::memory_order_relaxed) % ids.size();
            return ids[static_cast<size_t>(idx)];
        }
    }
    return ids.front();
}

std::string IngressSelector::chooseIngress(const std::string& serviceId) const {
    // RouteInformed: Ask the BGP oracle which PoP actually serves the request.
    if (cfg_.mode == IngressMode::RouteInformed && oracle_) {
        if (auto pop = oracle_->serving_pop(serviceId); pop && is_pop_eligible(*pop)) {
            return *pop;
        }
    }

    // PolicyDeterministic path: no client/flow key supplied -> defaults to RR.
    return choose_policy_deterministic(eligible_ids_, std::nullopt);
}

std::string IngressSelector::chooseIngress(const std::string& serviceId,
                                           const std::string& clientSrcIp) const {
    // RouteInformed can be client-aware
    if (cfg_.mode == IngressMode::RouteInformed && oracle_) {
        if (auto pop = oracle_->serving_pop(serviceId, clientSrcIp);
            pop && is_pop_eligible(*pop)) {
            return *pop;
        }
    }

    std::optional<uint64_t> h;
    if (cfg_.strategy == IngressStrategy::HashSourceIP ||
        cfg_.strategy == IngressStrategy::Hash5Tuple) {
        h = hash_ip_literal(clientSrcIp, cfg_.seed);
    }
    return choose_policy_deterministic(eligible_ids_, h);
}

std::string IngressSelector::chooseIngress(const std::string& serviceId,
                                           const Flow5Tuple& flow) const {
    if (cfg_.mode == IngressMode::RouteInformed && oracle_) {
        if (auto pop = oracle_->serving_pop(serviceId); pop && is_pop_eligible(*pop)) {
            return *pop;
        }
    }

    std::optional<uint64_t> h;
    if (cfg_.strategy == IngressStrategy::HashSourceIP) {
        h = hash_ipv4(flow.src_ip, cfg_.seed);
    } else if (cfg_.strategy == IngressStrategy::Hash5Tuple) {
        h = hash_five_tuple(flow, cfg_.seed);
    }
    return choose_policy_deterministic(eligible_ids_, h);
}

} // namespace alpha::routing
