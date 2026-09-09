#pragma once

#include "commute_sa/theta.h"

#include <string>
#include <vector>

namespace commute_sa {

/** Pure deterministic estimator used by the profile tool and unit smoke. */
double EstimateEvidenceStrengthValue(double globalStrength, int posHit, int posValid,
    int negHit, int negValid, int totalEpisodes);

/** Robust duration estimator. Fewer than three samples return the global mean. */
double EstimateDurationMeanValue(double globalMean, const std::vector<double> &samples,
    double *median = nullptr, double *trimmedMean = nullptr, double *mad = nullptr,
    double *confidence = nullptr);

/** Overlay all committed side/anchor profile fields on global theta. */
Theta ApplyCommittedUserAnchorProfile(
    const Theta &global, const std::string &anchorId);

/** Compatibility alias retained for existing callers. */
Theta ApplyCommittedEvidenceStrengthProfile(
    const Theta &global, const std::string &side, const std::string &anchorId);

std::string GetUserAnchorProfileAction(const std::string &paramsJson);
std::string GetPersonalizationHistorySummaryAction(const std::string &paramsJson);
std::string EstimateEvidenceStrengthAction(const std::string &paramsJson);
std::string GetEvidenceStrengthTrialAction(const std::string &paramsJson);
std::string CommitEvidenceStrengthCandidateAction(const std::string &paramsJson);
std::string DiscardEvidenceStrengthCandidateAction(const std::string &paramsJson);
std::string FitDurationPriorAction(const std::string &paramsJson);
std::string GetDurationPriorTrialAction(const std::string &paramsJson);
std::string CommitDurationPriorCandidateAction(const std::string &paramsJson);
std::string DiscardDurationPriorCandidateAction(const std::string &paramsJson);

}  // namespace commute_sa
