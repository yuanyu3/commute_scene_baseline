#include "commute_sa/leave_hsmm.h"

#include <iostream>

int main()
{
    using namespace commute_sa;

    LeaveHsmmConfig config;
    config.preleave_min_s = 5.0;
    config.preleave_mean_s = 20.0;
    config.preleave_max_s = 90.0;
    config.leaving_min_s = 5.0;
    config.leaving_mean_s = 30.0;
    config.leaving_max_s = 120.0;

    int64_t tMs = 1'000'000;
    LeaveObservation still;
    still.relation_known = true;
    still.inside = true;

    LeaveHsmm hsmm;
    LeaveHsmmResult result = hsmm.Step(still, tMs, config);
    for (int i = 0; i < 24; ++i) {
        tMs += 5000;
        result = hsmm.Step(still, tMs, config);
    }
    if (result.AtAnchorProbability() < 0.90 || result.LeavingProbability() > 0.05) {
        std::cerr << "FAIL: stable dwell drifted away from AT_ANCHOR p_at=" << result.AtAnchorProbability()
                  << " p_leave=" << result.LeavingProbability() << "\n";
        return 1;
    }

    LeaveObservation outbound;
    outbound.walking = 1.0;
    outbound.pdr_outbound = 0.85;
    outbound.geo_outbound = 0.80;
    outbound.wifi_detach = 0.65;
    outbound.cell_detach = 0.30;
    outbound.time_prior = 0.80;
    outbound.relation_known = true;
    outbound.near = true;

    bool reachedLeaving = false;
    for (int i = 0; i < 24; ++i) {
        tMs += 5000;
        result = hsmm.Step(outbound, tMs, config);
        if (result.LeavingProbability() >= 0.55) {
            reachedLeaving = true;
            break;
        }
    }
    if (!reachedLeaving) {
        std::cerr << "FAIL: sustained outbound evidence never reached LEAVING p="
                  << result.LeavingProbability() << " phase=" << LeavePhaseToString(result.phase) << "\n";
        return 1;
    }

    LeaveObservation outside = outbound;
    outside.near = false;
    outside.outside = true;
    outside.geo_outbound = 1.0;
    outside.wifi_detach = 1.0;
    for (int i = 0; i < 3; ++i) {
        tMs += 5000;
        result = hsmm.Step(outside, tMs, config);
    }
    if (result.OutsideProbability() < 0.70) {
        std::cerr << "FAIL: OUTSIDE evidence was not confirmed p=" << result.OutsideProbability() << "\n";
        return 1;
    }

    LeaveObservation returning = still;
    returning.approaching = true;
    returning.attached = true;
    for (int i = 0; i < 3; ++i) {
        tMs += 5000;
        result = hsmm.Step(returning, tMs, config);
    }
    if (result.AtAnchorProbability() < 0.70) {
        std::cerr << "FAIL: return evidence did not recover AT_ANCHOR p=" << result.AtAnchorProbability() << "\n";
        return 1;
    }

    LeaveHsmm walkingOnlyHsmm;
    LeaveObservation walkingOnly = still;
    walkingOnly.walking = 1.0;
    walkingOnly.time_prior = 0.8;
    result = walkingOnlyHsmm.Step(walkingOnly, tMs, config);
    for (int i = 0; i < 24; ++i) {
        tMs += 5000;
        result = walkingOnlyHsmm.Step(walkingOnly, tMs, config);
    }
    if (result.LeavingProbability() >= 0.55) {
        std::cerr << "FAIL: walking alone became a confident leave p=" << result.LeavingProbability() << "\n";
        return 1;
    }

    std::cout << "ok p_at=" << result.AtAnchorProbability() << " p_pre=" << result.PreLeaveProbability()
              << " p_leave=" << result.LeavingProbability() << "\n";
    return 0;
}
