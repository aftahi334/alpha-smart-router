/**
 * @file test_ingress.cpp
 * @brief Unit tests for IngressSelector (RoundRobin, HashSourceIP, Hash5Tuple,
 *        and RouteInformed with SimulatedBgpOracle).
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include "alpha/routing/bgp_oracle_sim.hpp"
#include "alpha/routing/ingress_selector.hpp"
#include "alpha/routing/pop.hpp"

namespace {

using alpha::routing::Flow5Tuple;
using alpha::routing::Health;
using alpha::routing::IngressConfig;
using alpha::routing::IngressMode;
using alpha::routing::IngressSelector;
using alpha::routing::IngressStrategy;
using alpha::routing::Pop;
using alpha::routing::PopList;
using alpha::routing::SimRoute;
using alpha::routing::SimRouteMap;
using alpha::routing::SimulatedBgpOracle;

TEST(IngressSelector, RoundRobin_CyclesEligiblePopsAndSkipsDown) {
  IngressSelector sel;
  sel.update_config(IngressConfig{
    .mode = IngressMode::PolicyDeterministic,
    .strategy = IngressStrategy::RoundRobin,
  });

  // Empty PoP list returns empty string
  EXPECT_TRUE(sel.chooseIngress("svc").empty());

  const PopList pops{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1", .health = Health::Up},
    Pop{.id = "ord", .region = "us-central", .ip = "192.0.2.2", .health = Health::Down},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1", .health = Health::Degraded},
  };
  sel.loadPops(pops);

  // "ord" is Down and must never be selected; RR alternates "nyc" and "sfo"
  EXPECT_EQ(sel.chooseIngress("svc"), "nyc");
  EXPECT_EQ(sel.chooseIngress("svc"), "sfo");
  EXPECT_EQ(sel.chooseIngress("svc"), "nyc");
  EXPECT_EQ(sel.chooseIngress("svc"), "sfo");

  // If all PoPs are Down, selection returns empty string
  sel.loadPops(PopList{
    Pop{.id = "ord", .region = "us-central", .ip = "192.0.2.2", .health = Health::Down},
  });
  EXPECT_TRUE(sel.chooseIngress("svc").empty());
}

TEST(IngressSelector, HashSourceIP_DeterministicAndDistributesAcrossClients) {
  constexpr std::uint64_t kSeed = 0x123456789ABCDEF0ULL;
  IngressSelector sel;
  sel.update_config(IngressConfig{
    .mode = IngressMode::PolicyDeterministic,
    .strategy = IngressStrategy::HashSourceIP,
    .seed = kSeed,
  });

  const PopList pops{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1"},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1"},
    Pop{.id = "fra", .region = "eu-central", .ip = "203.0.113.1"},
    Pop{.id = "nrt", .region = "ap-east", .ip = "203.0.113.2"},
  };
  sel.loadPops(pops);

  // IPv4 literal and canonical host-order uint32 produce identical hash
  const auto lit_hash = IngressSelector::hash_ip_literal("192.0.2.10", kSeed);
  ASSERT_TRUE(lit_hash.has_value());
  EXPECT_EQ(*lit_hash, IngressSelector::hash_ipv4(0xC000020Au, kSeed));

  // Identical IPv4 and IPv6 clients are pinned to the same PoP across repeated calls
  const std::string v4_choice = sel.chooseIngress("svc", "192.0.2.10");
  ASSERT_FALSE(v4_choice.empty());
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(sel.chooseIngress("svc", "192.0.2.10"), v4_choice);
  }

  const std::string v6_choice = sel.chooseIngress("svc", "2001:db8::42");
  ASSERT_FALSE(v6_choice.empty());
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(sel.chooseIngress("svc", "2001:db8::42"), v6_choice);
  }

  // Sweeping distinct client IPs distributes across multiple PoPs (not pinned to index 0)
  std::set<std::string> chosen_pops;
  for (int host = 1; host <= 32; ++host) {
    const std::string ip = "198.51.100." + std::to_string(host);
    chosen_pops.insert(sel.chooseIngress("svc", ip));
  }
  EXPECT_EQ(chosen_pops.size(), pops.size());
}

TEST(IngressSelector, Hash5Tuple_SourceOnlyVsFull5Tuple) {
  constexpr std::uint64_t kSeed = 0xCAFEBABE12345678ULL;
  const PopList pops{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1"},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1"},
    Pop{.id = "fra", .region = "eu-central", .ip = "203.0.113.1"},
    Pop{.id = "nrt", .region = "ap-east", .ip = "203.0.113.2"},
  };

  IngressSelector sel;
  sel.loadPops(pops);

  // Under HashSourceIP, flows with identical src_ip and different ports map to the same PoP
  sel.update_config(IngressConfig{
    .mode = IngressMode::PolicyDeterministic,
    .strategy = IngressStrategy::HashSourceIP,
    .seed = kSeed,
  });

  const Flow5Tuple base_flow{
    .src_ip = 0xC000020Au,   // 192.0.2.10
    .dst_ip = 0xCB007101u,   // 203.0.113.1
    .src_port = 50000,
    .dst_port = 443,
    .proto = 6,              // TCP
  };
  const std::string src_only_pop = sel.chooseIngress("svc", base_flow);
  for (std::uint16_t sport = 50001; sport < 50020; ++sport) {
    Flow5Tuple varied = base_flow;
    varied.src_port = sport;
    EXPECT_EQ(sel.chooseIngress("svc", varied), src_only_pop);
  }
  // Matches string clientSrcIp overload for "192.0.2.10"
  EXPECT_EQ(sel.chooseIngress("svc", "192.0.2.10"), src_only_pop);

  // Under Hash5Tuple, varying L4 ports distributes across multiple PoPs while
  // identical 5-tuples remain deterministically pinned
  sel.update_config(IngressConfig{
    .mode = IngressMode::PolicyDeterministic,
    .strategy = IngressStrategy::Hash5Tuple,
    .seed = kSeed,
  });

  const std::string pinned_flow_pop = sel.chooseIngress("svc", base_flow);
  EXPECT_EQ(sel.chooseIngress("svc", base_flow), pinned_flow_pop);

  std::set<std::string> five_tuple_pops;
  for (std::uint16_t sport = 50000; sport < 50032; ++sport) {
    Flow5Tuple varied = base_flow;
    varied.src_port = sport;
    five_tuple_pops.insert(sel.chooseIngress("svc", varied));
  }
  EXPECT_EQ(five_tuple_pops.size(), pops.size());
}

TEST(IngressSelector, HashFallback_WhenInputAbsentOrInvalid) {
  IngressSelector sel;
  sel.update_config(IngressConfig{
    .mode = IngressMode::PolicyDeterministic,
    .strategy = IngressStrategy::HashSourceIP,
  });

  const PopList pops{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1"},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1"},
  };
  sel.loadPops(pops);

  // Invalid IP literals return nullopt from hash_ip_literal
  EXPECT_FALSE(IngressSelector::hash_ip_literal("", 1).has_value());
  EXPECT_FALSE(IngressSelector::hash_ip_literal("not-an-ip", 1).has_value());
  EXPECT_FALSE(IngressSelector::hash_ip_literal(std::string_view("192.0.2.1\0junk", 14), 1).has_value());

  // Unkeyed chooseIngress(serviceId) and invalid IP strings fall back to RoundRobin
  EXPECT_EQ(sel.chooseIngress("svc"), "nyc");
  EXPECT_EQ(sel.chooseIngress("svc"), "sfo");
  EXPECT_EQ(sel.chooseIngress("svc", ""), "nyc");
  EXPECT_EQ(sel.chooseIngress("svc", "invalid_ip"), "sfo");
}

TEST(IngressSelector, RouteInformed_BgpOracleAndFallback) {
  auto oracle = std::make_shared<SimulatedBgpOracle>();
  SimRouteMap routes;
  routes["svc_bgp"] = {
    SimRoute{.pop_id = "nyc", .local_pref = 100, .as_path_len = 3, .med = 50, .igp_cost = 10},
    SimRoute{.pop_id = "sfo", .local_pref = 200, .as_path_len = 4, .med = 80, .igp_cost = 20},
    SimRoute{.pop_id = "fra", .local_pref = 200, .as_path_len = 2, .med = 90, .igp_cost = 30},
  };
  oracle->load_routes(std::move(routes));

  IngressSelector sel;
  sel.attachOracle(oracle);
  sel.update_config(IngressConfig{
    .mode = IngressMode::RouteInformed,
    .strategy = IngressStrategy::RoundRobin,
  });

  const PopList pops{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1", .health = Health::Up},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1", .health = Health::Up},
    Pop{.id = "fra", .region = "eu-central", .ip = "203.0.113.1", .health = Health::Up},
  };
  sel.loadPops(pops);

  // "fra" wins BGP tie-break (local_pref 200 tied with sfo, shorter as_path_len 2 < 4)
  EXPECT_EQ(sel.chooseIngress("svc_bgp"), "fra");
  EXPECT_EQ(sel.chooseIngress("svc_bgp", "192.0.2.55"), "fra");

  // Unknown service falls back to local deterministic policy (RoundRobin)
  EXPECT_EQ(sel.chooseIngress("svc_unknown"), "nyc");
  EXPECT_EQ(sel.chooseIngress("svc_unknown"), "sfo");

  // If the oracle's chosen PoP ("fra") is locally marked Health::Down, selector falls back
  sel.loadPops(PopList{
    Pop{.id = "nyc", .region = "us-east", .ip = "192.0.2.1", .health = Health::Up},
    Pop{.id = "sfo", .region = "us-west", .ip = "198.51.100.1", .health = Health::Up},
    Pop{.id = "fra", .region = "eu-central", .ip = "203.0.113.1", .health = Health::Down},
  });
  EXPECT_EQ(sel.chooseIngress("svc_bgp"), "nyc");
}

} // namespace

