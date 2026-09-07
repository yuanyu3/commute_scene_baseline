#pragma once

#include <string>

namespace commute_sa {

/**
 * Deterministic error-signature analysis over policy_history.jsonl.
 * Returns ranked causes plus a rule-generated semantic intervention plan.
 */
std::string AnalyzePersonalizationRulesAction(const std::string &paramsJson);

/**
 * Generate bounded theta candidates from semantic blocks/directions, replay the
 * same LeaveHsmm over history, and keep an in-memory best candidate trial.
 *
 * Accepted plan fields:
 *   primary_block, primary_direction,
 *   secondary_block, secondary_direction,
 *   objective, max_candidates, min_improvement.
 * No numeric theta value is accepted from the caller.
 */
std::string RunConstrainedThetaOptimizerAction(const std::string &paramsJson);

/** Rule-analysis + constrained optimizer convenience entry for the non-Agent baseline. */
std::string RunRulePersonalizationAction(const std::string &paramsJson);

/** Inspect, commit, or discard the candidate trial produced by the optimizer. */
std::string GetOptimizationTrialAction(const std::string &paramsJson);
std::string CommitOptimizedThetaAction(const std::string &paramsJson);
std::string DiscardOptimizationTrialAction(const std::string &paramsJson);

/** Deterministic profile summary plus validated Agent-proposed context memory. */
std::string GetPersonalizationProfileAction(const std::string &paramsJson);
std::string ProposeContextProfileUpdateAction(const std::string &paramsJson);

/** Validate and persist the Agent's structured hypotheses/intervention audit. */
std::string SubmitAgentAnalysisAction(const std::string &paramsJson);

}  // namespace commute_sa
