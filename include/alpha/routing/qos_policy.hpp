#pragma once
/**
 * @file qos_policy.hpp
 * @brief QoS policy: per-class thresholds + weighted scoring of candidate paths.
 * @details Read-mostly and deterministic. Provides DSCP mapping and path scoring
 *          based on normalized latency/jitter/loss vs class targets.
 */

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace alpha::routing {

/**
 * @enum QoSClass
 * @brief Application-level traffic classes (mapped to DSCP PHBs by config).
 */
enum class QoSClass : uint8_t {
    Bulk = 0,        ///< Backups/sync (latency-insensitive)
    BestEffort,      ///< Default class
    Interactive,     ///< Latency-sensitive but tolerant
    Realtime         ///< Voice/video, most stringent
};

/**
 * @struct QoSThresholds
 * @brief SLO-style targets used for normalization and compliance checks.
 */
struct QoSThresholds {
    uint32_t max_latency_us{10000};  ///< Target ceiling for latency (microseconds)
    uint32_t max_jitter_us{5000};    ///< Target ceiling for jitter (microseconds)
    double   max_loss{0.01};         ///< Target ceiling for packet loss (0.0–1.0)
};

/**
 * @struct QoSWeights
 * @brief Relative importance of each metric in the blended score.
 */
struct QoSWeights {
    double latency{0.6};  ///< Weight of latency component
    double jitter{0.3};   ///< Weight of jitter component
    double loss{0.1};     ///< Weight of loss component
};

/**
 * @struct PathMetrics
 * @brief Snapshot of path health metrics supplied by the telemetry collector.
 */
struct PathMetrics {
    std::string path_id;     ///< Stable identifier (e.g., "pop_sfo_primary")
    uint32_t    latency_us{0}; ///< RTT or one-way; be consistent across the system
    uint32_t    jitter_us{0};  ///< Jitter in microseconds
    double      loss{0.0};     ///< Packet loss ratio [0.0, 1.0]
};

/**
 * @struct QoSScore
 * @brief Scoring result for a path.
 */
struct QoSScore {
    std::string path_id;        ///< Path identifier scored
    double      score{0.0};     ///< Higher is better; range typically [0,1]
    bool        within_thresholds{true}; ///< True if all metrics meet class targets
};

/**
 * @struct QoSConfig
 * @brief Immutable configuration bundle for QoS scoring and DSCP mapping.
 */
struct QoSConfig {
    std::unordered_map<QoSClass, QoSThresholds> thresholds_by_class; ///< Targets per class
    QoSWeights weights;                                              ///< Blend weights
    std::unordered_map<QoSClass, uint8_t> dscp_by_class;             ///< DSCP (6 bits) per class
};

/**
 * @class QoSPolicy
 * @brief Concrete QoS policy. Thread-safe for concurrent readers.
 */
class QoSPolicy {
public:
    /** @brief Construct with an initial configuration. */
    explicit QoSPolicy(QoSConfig cfg) noexcept;

    /**
     * @brief Lookup DSCP codepoint (6 bits) for a class.
     * @param clazz Traffic class.
     * @return DSCP value (0..63). Returns 0 (BE) if unmapped.
     */
    uint8_t dscp(QoSClass clazz) const noexcept;

    /**
     * @brief Score a single path against a class's targets/weights.
     * @param pm Path metrics snapshot.
     * @param clazz Traffic class whose thresholds apply.
     * @return Scoring result with compliance flag.
     */
    QoSScore score_path(const PathMetrics& pm, QoSClass clazz) const noexcept;

    /**
     * @brief Choose the best candidate among paths.
     * @param candidates List of candidate path metrics.
     * @param clazz Traffic class.
     * @param require_within_thresholds If true, prefer only compliant paths; falls back to best overall if none.
     * @return The best score if candidates exist; std::nullopt otherwise.
     */
    std::optional<QoSScore> choose_best(const std::vector<PathMetrics>& candidates,
                                        QoSClass clazz,
                                        bool require_within_thresholds = false) const noexcept;

    /** @brief Access the current configuration (by value). */
    QoSConfig config() const;

    /**
     * @brief Atomically replace the configuration (single-writer expected).
     * @param cfg New configuration.
     */
    void update_config(QoSConfig cfg) noexcept;

private:
    /** @brief Normalize latency vs. target; <= target → ~1.0, otherwise decays toward 0. */
    static double normalize_latency(uint32_t latency_us, uint32_t target_us) noexcept;
    /** @brief Normalize jitter vs. target; <= target → ~1.0, otherwise decays toward 0. */
    static double normalize_jitter(uint32_t jitter_us,  uint32_t target_us) noexcept;
    /** @brief Normalize loss vs. target; <= target → ~1.0, otherwise decays toward 0. */
    static double normalize_loss(double loss,            double target) noexcept;

    /** @brief Blend normalized components with weights and clamp to [0,1]. */
    static double blend(double nlat, double njit, double nloss, const QoSWeights& w) noexcept;

private:
    QoSConfig cfg_; ///< Read-mostly; replaced wholesale via update_config()
};

} // namespace alpha::routing
