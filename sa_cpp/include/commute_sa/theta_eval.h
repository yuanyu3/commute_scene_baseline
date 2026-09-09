#pragma once

#include "commute_sa/theta.h"
#include "commute_sa/leave_hsmm.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace commute_sa {

/**
 * Counterfactual score of a candidate θ on historical leave windows.
 *
 * Prefers policy_history.jsonl HSMM observation replay (can evaluate w_*).
 * Falls back to recorded push P(LEAVING) vs enter_leave when obs are absent.
 */
std::string EvaluateThetaOnHistoryJson(const std::string &rootDir, const Theta &theta, int64_t sinceMs = 0,
    int maxEpisodes = 30);

/**
 * Replay history after a deterministic context adapter transforms each HSMM
 * observation. episodeStart is true for the first tick of every episode, so a
 * bounded template interpreter may reset its sequence state.
 * Optional prefix trace exposes causal posteriors/gates. cutoffMs truncates
 * inference only; partial replay scores are diagnostic, not fit objectives.
 */
using ObservationAdapter = std::function<void(LeaveObservation &observation, bool episodeStart)>;
struct ReplayEpisodeSummary {
    std::string key;
    bool positive = false;
    bool hard_negative = false;
    bool pushed = false;
    int64_t push_ms = 0;
    double lead_s = -1.0;
    bool aborted = false;
    int64_t cancel_ms = 0;
};
// Reject per-episode regressions; aggregate counts can hide swaps between episodes.
bool CheckReplayEpisodeSafety(const std::vector<ReplayEpisodeSummary> &baseline,
    const std::vector<ReplayEpisodeSummary> &candidate, std::string *reason);
std::string EvaluateThetaOnHistoryWithAdapterJson(const std::string &rootDir, const Theta &theta,
    const ObservationAdapter &adapter, int64_t sinceMs = 0, int maxEpisodes = 30,
    bool includePrefixTrace = false, int64_t cutoffMs = 0,
    std::vector<ReplayEpisodeSummary> *summaries = nullptr,
    const std::string &anchorId = "", const std::string &legacySide = "");

/**
 * Infer uncensored PRE_LEAVE/LEAVING dwell samples from labeled positive
 * episode posterior paths. The returned values are seconds and are intended
 * for robust, low-frequency personalization rather than online inference.
 */
std::vector<double> InferHsmmDurationSamples(const std::string &rootDir, const Theta &theta,
    const std::string &anchorId, const std::string &legacySide,
    const std::string &state, int maxEpisodes = 100);

/** Snapshot / restore live θ for agent trial loops (in-memory + file persist on revert). */
bool BeginThetaTrial(std::string *err = nullptr);
bool RevertThetaTrial(std::string *err = nullptr);
bool CommitThetaTrial(std::string *err = nullptr);
bool HasActiveThetaTrial();

}  // namespace commute_sa
