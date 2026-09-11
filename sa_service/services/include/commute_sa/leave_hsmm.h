#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace commute_sa {

enum class LeavePhase {
    kAtAnchor = 0,
    kPreLeave = 1,
    kLeaving = 2,
    kOutside = 3,
};

const char *LeavePhaseToString(LeavePhase phase);

/** Semantic observation in [0, 1], produced from one sensor tick. */
struct LeaveObservation {
    // Runtime/replay context. LeaveHsmm ignores these fields; bounded context
    // templates may use them to deterministically rebuild personalized evidence.
    int64_t t_ms = 0;
    std::string context_side;
    double baro_descent_m = 0.0;
    bool baro_stable_platform = false;
    bool baro_stable_platform_known = false;
    double walking = 0.0;
    double pdr_outbound = 0.0;
    double geo_outbound = 0.0;
    double wifi_detach = 0.0;
    double cell_detach = 0.0;
    double ble_detach = 0.0;
    double time_prior = 0.0;
    bool relation_known = false;
    bool inside = false;
    bool near = false;
    bool outside = false;
    bool approaching = false;
    bool attached = false;
    double baro_descending = 0.0;
    double baro_lower_platform = 0.0;
    bool baro_available = false;
    // Return semantics are consumed only by context templates, not by the
    // atomic HSMM emission vector.
    double baro_ascending = 0.0;
    double vertical_closure = 0.0;
    /**
     * Context-template observations are kept separate from atomic sensor
     * semantics.  The template reports temporal structure; it never rewrites
     * walking/PDR/radio/barometer values.
     */
    bool sequence_available = false;
    double sequence_progress = 0.0;
    double sequence_complete = 0.0;
    // -1 preserves legacy progress/completion fusion; [0,1] is calibrated prefix readiness.
    double sequence_ready = -1.0;
    double negative_pattern_match = 0.0;
    double cancel_sequence_match = 0.0;
    double sequence_reliability = 0.0;
    bool context_available = false;
    std::array<double, 4> context_scores {{0, 0, 0, 0}};
    double context_positive_strength = 0.0;
    double context_negative_strength = 0.0;
    double context_return_strength = 0.0;
    double context_absence = 0.0;
    double context_wait_s = 0.0;
};

struct LeaveHsmmConfig {
    double preleave_min_s = 10.0;
    double preleave_mean_s = 90.0;
    double preleave_max_s = 300.0;
    double leaving_min_s = 10.0;
    double leaving_mean_s = 120.0;
    double leaving_max_s = 600.0;
    double max_gap_s = 300.0;
    /** Duration/topology priors. Sensor observations never modify transitions. */
    double at_anchor_exit_hazard_per_s = 0.0030;
    double preleave_exit_to_leaving = 0.90;
    /** Reliability of walking, PDR, geo, Wi-Fi, Cell, BLE and time observations. */
    std::array<double, 9> reliability {{1.0, 1.0, 1.0, 0.8, 0.6, 0.4, 0.8, 0.8, 0.8}};
};

struct LeaveHsmmResult {
    LeavePhase phase = LeavePhase::kAtAnchor;
    std::array<double, 4> probability {{1.0, 0.0, 0.0, 0.0}};

    double AtAnchorProbability() const { return probability[0]; }
    double PreLeaveProbability() const { return probability[1]; }
    double LeavingProbability() const { return probability[2]; }
    double OutsideProbability() const { return probability[3]; }
};

/**
 * Online explicit-duration hidden semi-Markov filter.
 *
 * Probability mass is retained for each (phase, elapsed-second) hypothesis.
 * State exit hazards therefore depend on elapsed duration instead of using the
 * memoryless self-transition assumed by a plain HMM.
 */
class LeaveHsmm {
public:
    LeaveHsmm();

    LeaveHsmmResult Step(const LeaveObservation &observation, int64_t tMs, const LeaveHsmmConfig &config);
    void Reset();

private:
    static constexpr int kPhaseCount = 4;
    static constexpr int kMaxTrackedAgeS = 3600;

    using DurationMass = std::array<std::vector<double>, kPhaseCount>;

    void Initialize(const LeaveObservation &observation, int64_t tMs);
    double ExitProbability(LeavePhase phase, int ageS, int dtS, const LeaveObservation &observation,
        const LeaveHsmmConfig &config) const;
    std::array<double, kPhaseCount> EmissionLikelihood(const LeaveObservation &observation,
        const LeaveHsmmConfig &config) const;
    LeaveHsmmResult Summarize() const;

    DurationMass mass_;
    bool initialized_ = false;
    int64_t last_t_ms_ = 0;
};

}  // namespace commute_sa
