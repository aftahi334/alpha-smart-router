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

bool FailoverPolicy::allow_switch(std::chrono::steady_clock::time_point last,
                                  std::chrono::steady_clock::time_point now,
                                  uint32_t hold_ms) const noexcept {
    using namespace std::chrono;
    return last.time_since_epoch().count() == 0 ||
           duration_cast<milliseconds>(now - last).count() >= hold_ms;
}

std::optional<FailoverDecision>
FailoverPolicy::evaluate(const std::string& current,
                         const std::vector<QoSScore>& scores,
                         const std::vector<PathHealth>& health,
                         std::chrono::steady_clock::time_point now) const {
    // Find current health + score
    const auto cur_state = state_of(current, health);
    const QoSScore* cur_sc = find_score(scores, current);
    const auto cur_ph_it = std::find_if(health.begin(), health.end(),
                                        [&](const PathHealth& p){ return p.path_id == current; });
    const auto cur_last_change = (cur_ph_it == health.end()) ? std::chrono::steady_clock::time_point{}
                                                             : cur_ph_it->last_change;

    // Helper: best healthy candidate
    const QoSScore* best = nullptr;
    for (const auto& s : scores) {
        if (state_of(s.path_id, health) == HealthState::Down) continue;
        if (!best || s.score > best->score) best = &s;
    }
    if (!best) return std::nullopt; // nothing to do

    // If current is Down → switch immediately to best healthy
    if (cur_state == HealthState::Down) {
        return FailoverDecision{best->path_id, "current_down"};
    }

    // Stickiness: require improvement margin + min hold to switch
    if (cur_sc) {
        const double needed = cur_sc->score * (1.0 + cfg_.improve_pct_to_switch);
        if (best->path_id != current && best->score >= needed &&
            allow_switch(cur_last_change, now, cfg_.min_hold_ms)) {
            return FailoverDecision{best->path_id, "better_candidate_with_margin"};
        }
    } else {
        // No current score → pick best healthy
        return FailoverDecision{best->path_id, "no_current_score"};
    }

    // Return-to-primary logic
    if (cfg_.return_to_primary && !cfg_.primary_path_id.empty() &&
        cfg_.primary_path_id != current) {
        const QoSScore* prim = find_score(scores, cfg_.primary_path_id);
        const auto prim_state = state_of(cfg_.primary_path_id, health);
        const auto prim_ph_it = std::find_if(health.begin(), health.end(),
                                             [&](const PathHealth& p){ return p.path_id == cfg_.primary_path_id; });
        const auto prim_last_change = (prim_ph_it == health.end()) ? std::chrono::steady_clock::time_point{}
                                                                   : prim_ph_it->last_change;
        if (prim && prim_state != HealthState::Down &&
            prim->score >= (best ? best->score : 0.0) &&
            allow_switch(prim_last_change, now, cfg_.recovery_hold_ms)) {
            return FailoverDecision{cfg_.primary_path_id, "return_to_primary"};
        }
    }

    return std::nullopt; // keep current
}

} // namespace alpha::routing
