/**
 * @file qos_policy.cpp
 * @brief Implementation of QoSPolicy scoring and DSCP lookup.
 */
#include "alpha/routing/qos_policy.hpp"
#include <algorithm>
#include <utility>

namespace alpha::routing {

QoSPolicy::QoSPolicy(QoSConfig cfg) noexcept
    : cfg_(std::move(cfg)) {}

uint8_t QoSPolicy::dscp(QoSClass clazz) const noexcept {
    const auto it = cfg_.dscp_by_class.find(clazz);
    // Default to Best Effort (0) when unmapped.
    return it == cfg_.dscp_by_class.end() ? uint8_t{0} : it->second;
}

QoSScore QoSPolicy::score_path(const PathMetrics& pm, QoSClass clazz) const noexcept {
    QoSScore out{pm.path_id, 0.0, true};

    // Fetch thresholds for the class; fall back to conservative defaults.
    const auto th_it = cfg_.thresholds_by_class.find(clazz);
    const QoSThresholds th = (th_it == cfg_.thresholds_by_class.end())
        ? QoSThresholds{} : th_it->second;

    // Normalize each metric: ~1.0 means "meets target," <1.0 means "worse than target."
    const double nlat  = normalize_latency(pm.latency_us, th.max_latency_us);
    const double njit  = normalize_jitter(pm.jitter_us,   th.max_jitter_us);
    const double nloss = normalize_loss(pm.loss,          th.max_loss);

    // Binary compliance flag (useful for strict modes or observability tags).
    out.within_thresholds = (pm.latency_us <= th.max_latency_us) &&
                            (pm.jitter_us  <= th.max_jitter_us) &&
                            (pm.loss       <= th.max_loss);

    // Weighted blend into a single score; higher is better.
    out.score = blend(nlat, njit, nloss, cfg_.weights);
    return out;
}

std::optional<QoSScore> QoSPolicy::choose_best(const std::vector<PathMetrics>& candidates,
                                               QoSClass clazz,
                                               bool require_within_thresholds) const noexcept {
    std::optional<QoSScore> best;

    // First pass: pick best among compliant candidates (if required).
    for (const auto& pm : candidates) {
        QoSScore s = score_path(pm, clazz);
        if (require_within_thresholds && !s.within_thresholds) {
            continue;
        }
        if (!best.has_value() || s.score > best->score) {
            best = s;
        }
    }

    // Fallback: if nothing complied, choose best overall so we don't blackhole.
    if (!best.has_value() && require_within_thresholds) {
        for (const auto& pm : candidates) {
            QoSScore s = score_path(pm, clazz);
            if (!best.has_value() || s.score > best->score) {
                best = s;
            }
        }
    }

    return best;
}

QoSConfig QoSPolicy::config() const {
    return cfg_; // small value-type copy is intentional
}

void QoSPolicy::update_config(QoSConfig cfg) noexcept {
    // Single writer pattern expected (control-plane).
    cfg_ = std::move(cfg);
}

double QoSPolicy::normalize_latency(uint32_t latency_us, uint32_t target_us) noexcept {
    if (target_us == 0) return 0.0; // avoid div by zero; treat as non-compliant
    // <= target → 1.0; above target → decays smoothly toward 0.
    const double ratio = static_cast<double>(latency_us) / static_cast<double>(target_us);
    return 1.0 / (1.0 + std::max(0.0, ratio - 1.0));
}

double QoSPolicy::normalize_jitter(uint32_t jitter_us, uint32_t target_us) noexcept {
    if (target_us == 0) return 0.0;
    const double ratio = static_cast<double>(jitter_us) / static_cast<double>(target_us);
    return 1.0 / (1.0 + std::max(0.0, ratio - 1.0));
}

double QoSPolicy::normalize_loss(double loss, double target) noexcept {
    if (target <= 0.0) return 0.0;
    const double ratio = loss / target;
    return 1.0 / (1.0 + std::max(0.0, ratio - 1.0));
}

double QoSPolicy::blend(double nlat, double njit, double nloss, const QoSWeights& w) noexcept {
    const double sumw = std::max(1e-9, w.latency + w.jitter + w.loss);
    const double raw  = (nlat * w.latency + njit * w.jitter + nloss * w.loss) / sumw;
    return std::clamp(raw, 0.0, 1.0);
}

} // namespace alpha::routing
