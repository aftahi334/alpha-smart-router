/**
 * @file test_failover.cpp
 * @brief Unit tests for FailoverPolicy (current_down, selection dwell vs health
 *        transitions, primary recovery_hold_ms enforcement, and degraded policy).
 */

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "alpha/routing/failover_policy.hpp"

namespace {

using namespace std::chrono_literals;
using alpha::routing::FailoverConfig;
using alpha::routing::FailoverPolicy;
using alpha::routing::HealthState;
using alpha::routing::PathHealth;
using alpha::routing::QoSScore;

TEST(FailoverPolicy, CurrentDown_ImmediateSwitchAndAllDownNullopt) {
  const auto t0 = std::chrono::steady_clock::time_point{1000ms};
  FailoverPolicy policy(FailoverConfig{
    .primary_path_id = "p1",
    .return_to_primary = true,
    .improve_pct_to_switch = 0.15,
    .min_hold_ms = 5000,
    .recovery_hold_ms = 10000,
  });

  const std::vector<QoSScore> scores{
    {.path_id = "p1", .score = 0.90},
    {.path_id = "p2", .score = 0.75},
  };

  // Current path p1 is Down -> switches immediately to p2 despite min_hold_ms
  const std::vector<PathHealth> one_up{
    {.path_id = "p1", .state = HealthState::Down, .last_change = t0},
    {.path_id = "p2", .state = HealthState::Up, .last_change = t0},
  };
  const auto dec = policy.evaluate("p1", scores, one_up, t0 + 10ms);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(dec->next_path_id, "p2");
  EXPECT_EQ(dec->reason, "current_down");

  // All paths Down -> returns std::nullopt
  const std::vector<PathHealth> all_down{
    {.path_id = "p1", .state = HealthState::Down, .last_change = t0},
    {.path_id = "p2", .state = HealthState::Down, .last_change = t0},
  };
  EXPECT_FALSE(policy.evaluate("p2", scores, all_down, t0 + 20ms).has_value());
}

TEST(FailoverPolicy, RecoveryHold_EnforcedEvenWhenPrimaryExceedsMargin) {
  const auto t0 = std::chrono::steady_clock::time_point{10'000ms};
  FailoverPolicy policy(FailoverConfig{
    .primary_path_id = "primary",
    .return_to_primary = true,
    .improve_pct_to_switch = 0.10,
    .min_hold_ms = 2000,
    .recovery_hold_ms = 10000,
  });
  policy.note_switch("backup1", t0);

  // primary has a much higher score than backup1 (0.95 >= 0.60 * 1.10),
  // but primary only recovered at t0 + 3000ms.
  const auto t_recover = t0 + 3000ms;
  const std::vector<QoSScore> scores{
    {.path_id = "primary", .score = 0.95},
    {.path_id = "backup1", .score = 0.60},
  };
  const std::vector<PathHealth> health{
    {.path_id = "primary", .state = HealthState::Up, .last_change = t_recover},
    {.path_id = "backup1", .state = HealthState::Up, .last_change = t0},
  };

  // At t_recover + 100ms (and even at t_recover + 9999ms), min_hold_ms on backup1
  // has elapsed, but primary's 10,000ms recovery_hold_ms has NOT elapsed.
  // Neither better_candidate_with_margin nor return_to_primary may select primary.
  EXPECT_FALSE(policy.evaluate("backup1", scores, health, t_recover + 100ms).has_value());
  EXPECT_FALSE(policy.evaluate("backup1", scores, health, t_recover + 9999ms).has_value());

  // At t_recover + 10000ms, recovery_hold_ms is satisfied -> returns to primary
  const auto dec = policy.evaluate("backup1", scores, health, t_recover + 10000ms);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(dec->next_path_id, "primary");
  EXPECT_EQ(dec->reason, "return_to_primary");
}

TEST(FailoverPolicy, SelectionDwell_TrackedSeparatelyFromHealthTransitions) {
  const auto t0 = std::chrono::steady_clock::time_point{1000ms};
  FailoverPolicy policy(FailoverConfig{
    .primary_path_id = "p1",
    .return_to_primary = true,
    .improve_pct_to_switch = 0.10,
    .min_hold_ms = 3000,
    .recovery_hold_ms = 10000,
  });

  // All paths became Up at t0 (long ago), except p1 which goes Down at t1 = t0 + 20s
  const auto t1 = t0 + 20'000ms;
  const std::vector<PathHealth> health_p1_down{
    {.path_id = "p1", .state = HealthState::Down, .last_change = t1},
    {.path_id = "p2", .state = HealthState::Up, .last_change = t0},
    {.path_id = "p3", .state = HealthState::Up, .last_change = t0},
  };

  // Step 1: p1 goes Down at t1; p2 (0.70) beats p3 (0.65), so router switches to p2 at t1
  const std::vector<QoSScore> scores_step1{
    {.path_id = "p1", .score = 0.0},
    {.path_id = "p2", .score = 0.70},
    {.path_id = "p3", .score = 0.65},
  };
  const auto d1 = policy.evaluate("p1", scores_step1, health_p1_down, t1);
  ASSERT_TRUE(d1.has_value());
  EXPECT_EQ(d1->next_path_id, "p2");
  EXPECT_EQ(d1->reason, "current_down");

  // Step 2: 500ms later (t1 + 500ms), p3's score jumps to 0.90 (> 0.70 * 1.10).
  // Even though p2's PathHealth::last_change was t0 (20.5s ago), p2 was selected
  // only 500ms ago (< min_hold_ms = 3000ms), so switching must be suppressed.
  const std::vector<QoSScore> scores_step2{
    {.path_id = "p1", .score = 0.0},
    {.path_id = "p2", .score = 0.70},
    {.path_id = "p3", .score = 0.90},
  };
  EXPECT_FALSE(policy.evaluate("p2", scores_step2, health_p1_down, t1 + 500ms).has_value());

  // Step 3: At t1 + 3000ms, selection dwell (min_hold_ms) on p2 has elapsed.
  // Even if p2's health just transitioned to Degraded at t1 + 2900ms, selection
  // dwell is NOT reset by p2's health transition, so switching to p3 succeeds.
  const std::vector<PathHealth> health_p2_degraded{
    {.path_id = "p1", .state = HealthState::Down, .last_change = t1},
    {.path_id = "p2", .state = HealthState::Degraded, .last_change = t1 + 2900ms},
    {.path_id = "p3", .state = HealthState::Up, .last_change = t0},
  };
  const auto d3 = policy.evaluate("p2", scores_step2, health_p2_degraded, t1 + 3000ms);
  ASSERT_TRUE(d3.has_value());
  EXPECT_EQ(d3->next_path_id, "p3");
  EXPECT_EQ(d3->reason, "better_candidate_with_margin");
}

TEST(FailoverPolicy, DegradedPolicy_ConfigurableAllowOrReject) {
  const auto t0 = std::chrono::steady_clock::time_point{1000ms};
  FailoverPolicy policy(FailoverConfig{
    .primary_path_id = "p1",
    .return_to_primary = true,
    .improve_pct_to_switch = 0.20,
    .min_hold_ms = 5000,
    .recovery_hold_ms = 10000,
    .allow_degraded = false,
  });

  const std::vector<QoSScore> scores{
    {.path_id = "p1", .score = 0.85},
    {.path_id = "p2", .score = 0.80},
  };
  const std::vector<PathHealth> health{
    {.path_id = "p1", .state = HealthState::Degraded, .last_change = t0},
    {.path_id = "p2", .state = HealthState::Up, .last_change = t0},
  };

  // With allow_degraded = false, a Degraded current path triggers immediate failover
  const auto dec = policy.evaluate("p1", scores, health, t0 + 50ms);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(dec->next_path_id, "p2");
  EXPECT_EQ(dec->reason, "current_degraded");
}

} // namespace

