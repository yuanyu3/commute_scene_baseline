#pragma once

#include "commute_sa/leave_hsmm.h"

#include <cstdint>
#include <string>

namespace commute_sa {

/** Supported context-template DSL primitives and persisted active template. */
std::string GetContextTemplateCatalogAction(const std::string &paramsJson);
std::string GetActiveContextTemplateAction(const std::string &paramsJson);
/** Evaluate the persisted template at its frozen strength; never tunes on the evaluation history. */
std::string EvaluateActiveContextTemplateOnHistoryAction(const std::string &paramsJson);
/** Read-only model intervention: disable positive/negative/both sequence terms and replay. */
std::string DiagnoseContextTemplateOnHistoryAction(const std::string &paramsJson);
/**
 * Validate an Agent-proposed FALSE_PUSH -> ABORTED_LEAVE interpretation from
 * raw replay semantics and append a non-destructive label override.
 */
std::string ProposeAbortedLeaveInterpretationAction(const std::string &paramsJson);

/**
 * Validate an Agent-composed event sequence, replay LOW/MEDIUM/HIGH effect
 * strengths on historical HSMM observations, and stage the best safe template.
 * The Agent supplies structure only; numeric strengths are chosen in C++.
 */
std::string GenerateContextTemplateAction(const std::string &paramsJson);
std::string GetContextTemplateTrialAction(const std::string &paramsJson);
std::string CommitContextTemplateAction(const std::string &paramsJson);
std::string DiscardContextTemplateAction(const std::string &paramsJson);

/**
 * Apply the persisted, replay-approved template to one real-time HSMM
 * observation. Returns true only when an active template matched this side and
 * applicability. Numeric strength is read from the C++-selected profile, not
 * from the Agent request.
 */
bool ApplyActiveContextTemplateObservation(const std::string &side, const std::string &anchorId,
    int64_t tMs, LeaveObservation *observation);

/** Invalidate the in-process cache after a profile is committed externally. */
void ReloadActiveContextTemplateRuntime();

}  // namespace commute_sa
