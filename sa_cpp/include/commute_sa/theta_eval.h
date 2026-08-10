#pragma once

#include "commute_sa/theta.h"

#include <cstdint>
#include <string>

namespace commute_sa {

/**
 * Counterfactual score of a candidate θ on historical leave_episodes.jsonl.
 *
 * Uses recorded push scores + labels (no full GPS replay):
 * - Would candidate enter_leave still push? vs FALSE_PUSH / CONFIRMED_LEAVE
 * - lead_s vs candidate lead_min_s..lead_max_s
 *
 * Higher score is better. Also returns structured counts for the agent.
 */
std::string EvaluateThetaOnHistoryJson(const std::string &rootDir, const Theta &theta, int64_t sinceMs = 0,
    int maxEpisodes = 30);

/** Snapshot / restore live θ for agent trial loops (in-memory + file persist on revert). */
bool BeginThetaTrial(std::string *err = nullptr);
bool RevertThetaTrial(std::string *err = nullptr);
bool CommitThetaTrial(std::string *err = nullptr);
bool HasActiveThetaTrial();

}  // namespace commute_sa
