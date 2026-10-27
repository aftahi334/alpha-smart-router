/**
 * @file failover_policy.cpp
 * @brief Implementation of FailoverPolicy and helpers.
 */
#include "alpha/routing/failover_policy.hpp"
#include <algorithm>

namespace alpha::routing {

static const QoSScore* find_score(const std::vector<QoSScore>& v, const std::string& id) {
    for (const auto& s : v) if (s.path_id == id) return &s;
    return nullptr;
}

HealthState FailoverPolicy::state_of(const std::string& id, const std::vector<PathHealth>& h) {
    for (const auto& ph : h) if (ph.path_id == id) return ph.state;
    return HealthState::Down; // unknown → treat conservatively
}

std::chrono::steady_clock::time_point
FailoverPolicy::last_health_change_of(const std::string& id, const std::vector<PathHealth>& h) noexcept {
    for (const auto& ph : h) {
        if (ph.path_id == id) return ph.last_change;
    }
    return std::chrono::steady_clock::time_point{};
}

bool FailoverPolicy::allow_switch(std::chrono::steady_clock::time_point since,
                                  std::chrono::steady_clock::time_point now,
                                  uint32_t hold_ms) const noexcept {
    using namespace std::chrono;
    if (since.time_since_epoch().count() == 0) return true;
    if (now < since) return false;
    return duration_cast<milliseconds>(now - since).count() >= hold_ms;
}

std::optional<FailoverDecision>
FailoverPolicy::evaluate(const std::string& current,
                         const std::vector<QoSScore>& scores,
                         const std::vector<PathHealth>& health,
                         std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point last_switch_time) const {
    const auto cur_state = state_of(current, health);
    const QoSScore* cur_sc = find_score(scores, current);

    const bool has_primary = !cfg_.primary_path_id.empty();
    const auto prim_last_change = has_primary
        ? last_health_change_of(cfg_.primary_path_id, health)
        : std::chrono::steady_clock::time_point{};
    const bool prim_recovery_ready = !has_primary ||
        allow_switch(prim_last_change, now, cfg_.recovery_hold_ms);

    auto is_candidate_eligible = [&](const std::string& id) -> bool {
        const HealthState st = state_of(id, health);
        if (st == HealthState::Down) return false;
        if (!cfg_.allow_degraded && st == HealthState::Degraded) return false;
        // Enforce primary recovery hold across every branch that can select it
        if (has_primary && id == cfg_.primary_path_id && id != current && !prim_recovery_ready) {
            return false;
        }
        return true;
    };

    // Find best eligible candidate
    const QoSScore* best = nullptr;
    for (const auto& s : scores) {
        if (!is_candidate_eligible(s.path_id)) continue;
        if (!best || s.score > best->score) best = &s;
    }
    if (!best) return std::nullopt; // no eligible candidate

    // 1. If current is Down (or Degraded when allow_degraded is false) -> switch immediately
    if (cur_state == HealthState::Down) {
        return FailoverDecision{best->path_id, "current_down"};
    }
    if (!cfg_.allow_degraded && cur_state == HealthState::Degraded) {
        return FailoverDecision{best->path_id, "current_degraded"};
    }

    // 2. If current has no QoS score -> pick best eligible
    if (!cur_sc) {
        return FailoverDecision{best->path_id, "no_current_score"};
    }

    const bool dwell_ready = allow_switch(last_switch_time, now, cfg_.min_hold_ms);

    // 3. Return-to-primary logic (evaluated before margin-based switching so it is
    //    not shadowed by better_candidate_with_margin when primary recovers)
    if (cfg_.return_to_primary && has_primary && cfg_.primary_path_id != current) {
        const QoSScore* prim = find_score(scores, cfg_.primary_path_id);
        const auto prim_state = state_of(cfg_.primary_path_id, health);
        if (prim && prim_state == HealthState::Up &&
            prim_recovery_ready && dwell_ready &&
            prim->score >= best->score) {
            return FailoverDecision{cfg_.primary_path_id, "return_to_primary"};
        }
    }

    // 4. Stickiness: require relative improvement margin + selection dwell (min_hold_ms)
    const double needed = cur_sc->score * (1.0 + cfg_.improve_pct_to_switch);
    if (best->path_id != current && best->score >= needed && dwell_ready) {
        return FailoverDecision{best->path_id, "better_candidate_with_margin"};
    }

    return std::nullopt; // keep current
}

std::optional<FailoverDecision>
FailoverPolicy::evaluate(const std::string& current,
                         const std::vector<QoSScore>& scores,
                         const std::vector<PathHealth>& health,
                         std::chrono::steady_clock::time_point now) const {
    if (!tracked_path_id_.empty() && tracked_path_id_ != current) {
        tracked_path_id_  = current;
        last_switch_time_ = now;
    }

    auto decision = evaluate(current, scores, health, now, last_switch_time_);
    if (decision.has_value() && decision->next_path_id != current) {
        tracked_path_id_  = decision->next_path_id;
        last_switch_time_ = now;
    } else if (tracked_path_id_.empty() && !current.empty()) {
        tracked_path_id_ = current;
    }
    return decision;
}

} // namespace alpha::routing
