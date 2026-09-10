#include "commute_sa/leave_hsmm.h"

#include <cmath>
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

    // Atomic lower_platform is an Agent-template primitive, not a generic HSMM
    // emission. Toggling it alone must leave every state probability unchanged.
    LeaveHsmm noLowerHsmm;
    LeaveHsmm lowerHsmm;
    LeaveObservation noLower = outbound;
    noLower.baro_available = true;
    noLower.baro_descending = 0.4;
    noLower.baro_lower_platform = 0.0;
    LeaveObservation lower = noLower;
    lower.baro_lower_platform = 1.0;
    int64_t lowerT = tMs + 10000;
    for (int i = 0; i < 12; ++i) {
        lowerT += 5000;
        const auto a = noLowerHsmm.Step(noLower, lowerT, config);
        const auto b = lowerHsmm.Step(lower, lowerT, config);
        for (size_t state = 0; state < a.probability.size(); ++state) {
            if (std::abs(a.probability[state] - b.probability[state]) > 1e-12) {
                std::cerr << "FAIL: atomic lower_platform directly changed generic HSMM state="
                          << state << " absent=" << a.probability[state]
                          << " present=" << b.probability[state] << "\n";
                return 1;
            }
        }
    }

    // Context templates add interaction evidence; they must not rewrite the
    // identical atomic observation shared by all three filters.
    LeaveHsmm neutralHsmm;
    LeaveHsmm positiveHsmm;
    LeaveHsmm negativeHsmm;
    LeaveHsmm zeroSequenceHsmm;
    LeaveObservation neutral = outbound;
    neutral.near = false;
    neutral.inside = true;
    LeaveObservation positive = neutral;
    positive.sequence_available = true;
    positive.sequence_progress = 1.0;
    positive.sequence_complete = 1.0;
    positive.sequence_reliability = 0.4;
    LeaveObservation negative = neutral;
    negative.sequence_available = true;
    negative.negative_pattern_match = 1.0;
    negative.sequence_reliability = 0.4;
    LeaveObservation zeroSequence = positive;
    zeroSequence.sequence_reliability = 0.0;
    int64_t sequenceT = tMs + 10000;
    LeaveHsmmResult neutralResult;
    LeaveHsmmResult positiveResult;
    LeaveHsmmResult negativeResult;
    LeaveHsmmResult zeroSequenceResult;
    for (int i = 0; i < 12; ++i) {
        sequenceT += 5000;
        neutralResult = neutralHsmm.Step(neutral, sequenceT, config);
        positiveResult = positiveHsmm.Step(positive, sequenceT, config);
        negativeResult = negativeHsmm.Step(negative, sequenceT, config);
        zeroSequenceResult = zeroSequenceHsmm.Step(zeroSequence, sequenceT, config);
    }
    std::cout << "sequence_effect neutral=" << neutralResult.LeavingProbability()
              << " positive=" << positiveResult.LeavingProbability()
              << " negative=" << negativeResult.LeavingProbability() << "\n";
    if (positiveResult.LeavingProbability() <= neutralResult.LeavingProbability() ||
        negativeResult.LeavingProbability() >= neutralResult.LeavingProbability()) {
        std::cerr << "FAIL: independent sequence evidence direction is wrong neutral="
                  << neutralResult.LeavingProbability() << " positive="
                  << positiveResult.LeavingProbability() << " negative="
                  << negativeResult.LeavingProbability() << "\n";
        return 1;
    }
    if (std::abs(zeroSequenceResult.LeavingProbability() - neutralResult.LeavingProbability()) > 1e-12) {
        std::cerr << "FAIL: zero-reliability sequence changed HSMM\n";
        return 1;
    }

    // Full completion must not double-count calibrated prefix readiness.
    LeaveHsmm prefixFilter, completeFilter;
    LeaveObservation prefixObs = positive;
    prefixObs.sequence_ready = 1.0;
    prefixObs.sequence_progress = 0.33;
    prefixObs.sequence_complete = 0.0;
    LeaveObservation completeObs = prefixObs;
    completeObs.sequence_progress = 1.0;
    completeObs.sequence_complete = 1.0;
    for (int i = 0; i < 20; ++i) {
        const auto a = prefixFilter.Step(prefixObs, sequenceT + i * 5000, config);
        const auto b = completeFilter.Step(completeObs, sequenceT + i * 5000, config);
        for (size_t s = 0; s < 4; ++s) {
            if (std::abs(a.probability[s] - b.probability[s]) > 1e-12) {
                std::cerr << "FAIL: prefix/completion evidence double counted\n";
                return 1;
            }
        }
    }

    LeaveHsmmConfig disabledConfig = config;
    disabledConfig.reliability.fill(0.0);
    LeaveHsmm disabledQuiet;
    LeaveHsmm disabledStrong;
    LeaveObservation strong = still;
    strong.walking = 1.0;
    strong.pdr_outbound = 1.0;
    strong.geo_outbound = 1.0;
    strong.wifi_detach = 1.0;
    strong.attached = true;
    strong.cell_detach = 1.0;
    strong.ble_detach = 1.0;
    strong.time_prior = 1.0;
    strong.baro_available = true;
    strong.baro_descending = 1.0;
    strong.baro_lower_platform = 1.0;
    int64_t disabledT = tMs + 10000;
    auto quietResult = disabledQuiet.Step(still, disabledT, disabledConfig);
    auto strongResult = disabledStrong.Step(strong, disabledT, disabledConfig);
    for (int i = 0; i < 12; ++i) {
        disabledT += 5000;
        quietResult = disabledQuiet.Step(still, disabledT, disabledConfig);
        strongResult = disabledStrong.Step(strong, disabledT, disabledConfig);
    }
    for (size_t state = 0; state < quietResult.probability.size(); ++state) {
        if (std::abs(quietResult.probability[state] - strongResult.probability[state]) > 1e-12) {
            std::cerr << "FAIL: zero-reliability observation changed HSMM state=" << state
                      << " quiet=" << quietResult.probability[state]
                      << " strong=" << strongResult.probability[state] << "\n";
            return 1;
        }
    }

    std::cout << "ok p_at=" << result.AtAnchorProbability() << " p_pre=" << result.PreLeaveProbability()
              << " p_leave=" << result.LeavingProbability() << "\n";
    return 0;
}
