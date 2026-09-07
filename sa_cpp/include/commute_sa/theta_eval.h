#pragma once

#include "commute_sa/theta.h"
#include "commute_sa/leave_hsmm.h"

#include <cstdint>
#include <functional>
#include <string>

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
 */
using ObservationAdapter = std::function<void(LeaveObservation &observation, bool episodeStart)>;
std::string EvaluateThetaOnHistoryWithAdapterJson(const std::string &rootDir, const Theta &theta,
    const ObservationAdapter &adapter, int64_t sinceMs = 0, int maxEpisodes = 30);

/** Snapshot / restore live θ for agent trial loops (in-memory + file persist on revert). */
bool BeginThetaTrial(std::string *err = nullptr);
bool RevertThetaTrial(std::string *err = nullptr);
bool CommitThetaTrial(std::string *err = nullptr);
bool HasActiveThetaTrial();

}  // namespace commute_sa
