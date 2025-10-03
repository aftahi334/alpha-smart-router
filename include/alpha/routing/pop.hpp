/**
 * @file pop.hpp
 * @brief Common Point-of-Presence (PoP) model shared across routing components.
 *
 * Defines the health state and the `Pop` descriptor used by the service
 * registry, ingress selector, and path selection logic. Centralizing this
 * type avoids ODR issues and keeps comparisons consistent across modules.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace alpha::routing {

/**
 * @brief Health state reported for a PoP.
 *
 * @note Semantics:
 *  - Up:        Eligible for selection.
 *  - Degraded:  Eligible, but may be deprioritized by policies.
 *  - Down:      Ineligible for selection.
 */
enum class Health : std::uint8_t {
  Up = 0,
  Degraded = 1,
  Down = 2
};

/**
 * @brief Minimal PoP descriptor.
 *
 * Keep this type simple and move-friendly. Equality is defaulted so containers
 * like `std::vector<Pop>` compare element-wise (used by copy-on-write diffs in
 * the service registry).
 *
 * @note No uniqueness is enforced here; higher layers (e.g. registry) should
 *       ensure `id` uniqueness per service.
 */
struct Pop final {
  /// Human-readable PoP identifier, e.g. "NYC".
  std::string id;

  /// Region/group label, e.g. "us-east".
  std::string region;

  /// Control-plane address (IPv4/IPv6 literal as string).
  std::string ip;

  /// Optional weight for load balancing (default = 100).
  std::uint16_t weight{100};

  /// Reported health (default = Up).
  Health health{Health::Up};

  /// Structural equality (compares all fields).
  bool operator==(const Pop&) const = default;
};

/**
 * @brief Convenience alias for a list of PoPs.
 */
using PopList = std::vector<Pop>;

} // namespace alpha::routing
