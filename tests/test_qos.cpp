/**
 * @file test_qos.cpp
 * @brief Tests for CP/DP metric model conversion, header composition, and QoSPolicy scoring.
 */
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <vector>

#include "alpha/config/config_loader.hpp"
#include "alpha/config/constants.hpp"
#include "alpha/routing/path_selection.hpp"
#include "alpha/routing/qos_policy.hpp"

using alpha::routing::DpPathMetrics;
using alpha::routing::PathMetrics;
using alpha::routing::QoSClass;
using alpha::routing::QoSPathMetrics;
using alpha::routing::QoSPolicy;
using alpha::routing::kMaxLossPpm;
using alpha::routing::loss_ppm_to_ratio;
using alpha::routing::loss_ratio_to_ppm;
using alpha::routing::to_dp_metrics;
using alpha::routing::to_qos_metrics;

TEST(RoutingDataModel, LossUnitConversionAndClamping) {
  EXPECT_EQ(loss_ratio_to_ppm(-0.5), 0u);
  EXPECT_EQ(loss_ratio_to_ppm(std::numeric_limits<double>::quiet_NaN()), 0u);
  EXPECT_EQ(loss_ratio_to_ppm(0.0), 0u);
  EXPECT_EQ(loss_ratio_to_ppm(0.005), 5000u);
  EXPECT_EQ(loss_ratio_to_ppm(1.0), kMaxLossPpm);
  EXPECT_EQ(loss_ratio_to_ppm(1.5), kMaxLossPpm);

  EXPECT_DOUBLE_EQ(loss_ppm_to_ratio(0u), 0.0);
  EXPECT_DOUBLE_EQ(loss_ppm_to_ratio(5000u), 0.005);
  EXPECT_DOUBLE_EQ(loss_ppm_to_ratio(kMaxLossPpm), 1.0);
  EXPECT_DOUBLE_EQ(loss_ppm_to_ratio(kMaxLossPpm + 500u), 1.0);
}

TEST(RoutingDataModel, CpToDpAndBackRoundTrip) {
  const QoSPathMetrics cp_metrics{
    .path_id = "pop_sfo_primary",
    .latency_us = 3500,
    .jitter_us = 800,
    .loss = 0.0025
  };

  const DpPathMetrics dp_metrics = to_dp_metrics(
      cp_metrics,
      /*healthy=*/true,
      static_cast<std::uint8_t>(QoSClass::Realtime),
      /*avail_kbps=*/250000,
      /*one_way_delay_us=*/1750);

  EXPECT_EQ(dp_metrics.rtt_us, 3500u);
  EXPECT_EQ(dp_metrics.one_way_delay_us, 1750u);
  EXPECT_EQ(dp_metrics.jitter_us, 800u);
  EXPECT_EQ(dp_metrics.loss_ppm, 2500u);
  EXPECT_EQ(dp_metrics.avail_kbps, 250000u);
  EXPECT_EQ(dp_metrics.qos_class, static_cast<std::uint8_t>(QoSClass::Realtime));
  EXPECT_TRUE(dp_metrics.healthy);

  const QoSPathMetrics round_trip = to_qos_metrics("pop_sfo_primary", dp_metrics);
  EXPECT_EQ(round_trip.path_id, "pop_sfo_primary");
  EXPECT_EQ(round_trip.latency_us, 3500u);
  EXPECT_EQ(round_trip.jitter_us, 800u);
  EXPECT_DOUBLE_EQ(round_trip.loss, 0.0025);
}

TEST(QoSPolicy, ScoreAndChooseBest) {
  const auto router_cfg = alpha::config::Loader::load_from_file("unused.toml");
  const QoSPolicy policy(router_cfg.qos);

  EXPECT_EQ(policy.dscp(QoSClass::Realtime), alpha::config::constants::DSCP_EF);

  const std::vector<QoSPathMetrics> candidates{
    {.path_id = "compliant", .latency_us = 2500, .jitter_us = 500, .loss = 0.001},
    {.path_id = "degraded",  .latency_us = 12000, .jitter_us = 4000, .loss = 0.03}
  };

  const auto compliant_score = policy.score_path(candidates[0], QoSClass::Realtime);
  const auto degraded_score  = policy.score_path(candidates[1], QoSClass::Realtime);
  EXPECT_TRUE(compliant_score.within_thresholds);
  EXPECT_DOUBLE_EQ(compliant_score.score, 1.0);
  EXPECT_FALSE(degraded_score.within_thresholds);
  EXPECT_LT(degraded_score.score, compliant_score.score);

  const auto best = policy.choose_best(candidates, QoSClass::Realtime, /*require_within_thresholds=*/true);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->path_id, "compliant");

  // Negative loss must be rejected as non-compliant rather than treated as <= threshold.
  const auto bad_loss = policy.score_path(
      {.path_id = "negative_loss", .latency_us = 1000, .jitter_us = 100, .loss = -0.01},
      QoSClass::Realtime);
  EXPECT_FALSE(bad_loss.within_thresholds);
}
