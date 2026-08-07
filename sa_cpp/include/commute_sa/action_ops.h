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

/** Static param_limits for agent guidance. */
std::string GetParamLimitsJson();

}  // namespace commute_sa
