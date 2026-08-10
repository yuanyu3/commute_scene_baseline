#pragma once

#include <string>

namespace commute_sa {

/**
 * Action helpers for θ personalizer tools (JSON in / JSON out).
 * Prefer live BaselineRuntime engine when enabled; else edit product files.
 */
std::string ApplyThetaDeltaAction(const std::string &paramsJson);
std::string WriteAuditAction(const std::string &paramsJson);
std::string RequestAnchorReestimateAction(const std::string &paramsJson);

/** Counterfactual eval of current θ on leave_episodes history. */
std::string EvaluateThetaOnHistoryAction(const std::string &paramsJson);
/** Snapshot θ before a try→eval→revert/commit loop. */
std::string BeginThetaTrialAction(const std::string &paramsJson);
std::string RevertThetaTrialAction(const std::string &paramsJson);
std::string CommitThetaTrialAction(const std::string &paramsJson);

/** Static param_limits for agent guidance. */
std::string GetParamLimitsJson();

}  // namespace commute_sa
