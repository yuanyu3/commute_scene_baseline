#include "commute_sa/leave_hsmm.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace commute_sa {
namespace {

constexpr double kProbabilityFloor = 1e-9;

double Clip01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

int PhaseIndex(LeavePhase phase)
{
    return static_cast<int>(phase);
}

double FractionalBernoulliLogLikelihood(double observation, double expected)
{
    const double x = Clip01(observation);
    const double mu = std::max(0.02, std::min(0.98, expected));
    return x * std::log(mu) + (1.0 - x) * std::log(1.0 - mu);
}

}  // namespace

const char *LeavePhaseToString(LeavePhase phase)
{
    switch (phase) {
        case LeavePhase::kAtAnchor:
            return "AT_ANCHOR";
        case LeavePhase::kPreLeave:
            return "PRE_LEAVE";
        case LeavePhase::kLeaving:
            return "LEAVING";
        case LeavePhase::kOutside:
            return "OUTSIDE";
        default:
            return "AT_ANCHOR";
    }
}

LeaveHsmm::LeaveHsmm()
{
    for (auto &stateMass : mass_) {
        stateMass.assign(kMaxTrackedAgeS + 1, 0.0);
    }
}

void LeaveHsmm::Reset()
{
    for (auto &stateMass : mass_) {
        std::fill(stateMass.begin(), stateMass.end(), 0.0);
    }
    initialized_ = false;
    last_t_ms_ = 0;
}

void LeaveHsmm::Initialize(const LeaveObservation &observation, int64_t tMs)
{
    for (auto &stateMass : mass_) {
        std::fill(stateMass.begin(), stateMass.end(), 0.0);
    }
    const LeavePhase initial = observation.outside ? LeavePhase::kOutside : LeavePhase::kAtAnchor;
    mass_[PhaseIndex(initial)][0] = 1.0;
    initialized_ = true;
    last_t_ms_ = tMs;
}

double LeaveHsmm::ExitProbability(LeavePhase phase, int ageS, int dtS, const LeaveObservation &observation,
    const LeaveHsmmConfig &config) const
{
    const double dt = static_cast<double>(dtS);
    if (phase == LeavePhase::kAtAnchor) {
        // Keep a small observation-independent candidate flow into PRE_LEAVE.
        // Atomic sensors are consumed once, by EmissionLikelihood().
        return Clip01(1.0 - std::exp(-dt * std::max(0.0, config.at_anchor_exit_hazard_per_s)));
    }

    if (phase == LeavePhase::kPreLeave) {
        if (ageS < static_cast<int>(config.preleave_min_s)) {
            return 0.0;
        }
        if (ageS >= static_cast<int>(config.preleave_max_s)) {
            return 1.0;
        }
        const double scale = std::max(5.0, config.preleave_mean_s - config.preleave_min_s);
        return Clip01(1.0 - std::exp(-dt / scale));
    }

    if (phase == LeavePhase::kLeaving) {
        if (observation.outside) {
            return 0.95;
        }
        if (ageS < static_cast<int>(config.leaving_min_s)) {
            return 0.0;
        }
        if (ageS >= static_cast<int>(config.leaving_max_s)) {
            return 1.0;
        }
        const double scale = std::max(5.0, config.leaving_mean_s - config.leaving_min_s);
        return Clip01(1.0 - std::exp(-dt / scale));
    }

    // OUTSIDE only exits on a structural relation change. Attach/approach are
    // emission and product-gate evidence, not a second transition use.
    if (observation.inside) {
        return 0.95;
    }
    return 0.0;
}

std::array<double, LeaveHsmm::kPhaseCount> LeaveHsmm::EmissionLikelihood(
    const LeaveObservation &observation, const LeaveHsmmConfig &config) const
{
    const std::array<double, 9> x {{observation.walking, observation.pdr_outbound, observation.geo_outbound,
        observation.wifi_detach, observation.cell_detach, observation.ble_detach, observation.time_prior,
        observation.baro_descending, observation.baro_lower_platform}};
    const std::array<std::array<double, 9>, kPhaseCount> expected {{
        {{0.08, 0.03, 0.03, 0.05, 0.08, 0.08, 0.25, 0.03, 0.02}},
        {{0.65, 0.24, 0.12, 0.16, 0.12, 0.10, 0.62, 0.30, 0.08}},
        {{0.92, 0.72, 0.72, 0.62, 0.40, 0.24, 0.72, 0.80, 0.72}},
        {{0.65, 0.55, 0.96, 0.88, 0.62, 0.30, 0.45, 0.08, 0.82}},
    }};

    std::array<double, kPhaseCount> logLikelihood {};
    for (int state = 0; state < kPhaseCount; ++state) {
        double value = 0.0;
        for (size_t feature = 0; feature < x.size(); ++feature) {
            if (feature >= 7 && !observation.baro_available) continue;
            const double reliability = std::max(0.0, config.reliability[feature]);
            value += reliability * FractionalBernoulliLogLikelihood(x[feature], expected[state][feature]);
        }

        if (observation.relation_known) {
            double relationExpected = 0.25;
            if (observation.inside) {
                // INSIDE is a structural observation: it strongly rejects
                // OUTSIDE but still permits predictive LEAVING before the gate.
                // It must not inspect atomic sensors again.
                const std::array<double, kPhaseCount> p {{0.65, 0.65, 0.65, 0.02}};
                relationExpected = p[state];
            } else if (observation.near) {
                const std::array<double, kPhaseCount> p {{0.18, 0.48, 0.72, 0.12}};
                relationExpected = p[state];
            } else if (observation.outside) {
                const std::array<double, kPhaseCount> p {{0.01, 0.03, 0.10, 0.97}};
                relationExpected = p[state];
            }
            value += 3.0 * std::log(std::max(kProbabilityFloor, relationExpected));
        }

        if (observation.approaching || observation.attached) {
            const std::array<double, kPhaseCount> p {{0.92, 0.30, 0.02, 0.08}};
            value += 2.5 * std::log(p[state]);
        }
        if (observation.sequence_available) {
            // These are interaction terms: they encode temporal order that is
            // absent from the atomic per-tick observations.  Zero is neutral.
            const std::array<double, kPhaseCount> progressLlr {{-0.35, 0.65, 0.25, -0.30}};
            const std::array<double, kPhaseCount> completeLlr {{-1.00, 0.15, 1.20, -0.35}};
            const std::array<double, kPhaseCount> negativeLlr {{0.80, 0.25, -1.00, -0.25}};
            const double reliability = Clip01(observation.sequence_reliability);
            value += 2.0 * reliability * Clip01(observation.sequence_progress) * progressLlr[state];
            value += 2.0 * reliability * Clip01(observation.sequence_complete) * completeLlr[state];
            value += 2.0 * reliability * Clip01(observation.negative_pattern_match) * negativeLlr[state];
        }
        logLikelihood[state] = value;
    }

    const double maxLog = *std::max_element(logLikelihood.begin(), logLikelihood.end());
    std::array<double, kPhaseCount> likelihood {};
    for (int state = 0; state < kPhaseCount; ++state) {
        likelihood[state] = std::max(kProbabilityFloor, std::exp(logLikelihood[state] - maxLog));
    }
    return likelihood;
}

LeaveHsmmResult LeaveHsmm::Step(
    const LeaveObservation &observation, int64_t tMs, const LeaveHsmmConfig &config)
{
    LeaveObservation effective = observation;
    if (config.reliability[0] <= 0.0) effective.walking = 0.0;
    if (config.reliability[1] <= 0.0) effective.pdr_outbound = 0.0;
    if (config.reliability[2] <= 0.0) effective.geo_outbound = 0.0;
    if (config.reliability[3] <= 0.0) {
        effective.wifi_detach = 0.0;
        effective.attached = false;
    }
    if (config.reliability[4] <= 0.0) effective.cell_detach = 0.0;
    if (config.reliability[5] <= 0.0) effective.ble_detach = 0.0;
    if (config.reliability[6] <= 0.0) effective.time_prior = 0.0;
    if (config.reliability[7] <= 0.0 && config.reliability[8] <= 0.0) {
        effective.baro_descending = 0.0;
        effective.baro_lower_platform = 0.0;
        effective.baro_available = false;
    }
    if (!initialized_) {
        Initialize(effective, tMs);
        return Summarize();
    }

    const double rawDtS = static_cast<double>(tMs - last_t_ms_) / 1000.0;
    if (rawDtS <= 0.0 || rawDtS > config.max_gap_s) {
        Initialize(effective, tMs);
        return Summarize();
    }
    const int dtS = std::max(1, std::min(60, static_cast<int>(std::lround(rawDtS))));
    last_t_ms_ = tMs;

    DurationMass predicted;
    for (auto &stateMass : predicted) {
        stateMass.assign(kMaxTrackedAgeS + 1, 0.0);
    }

    for (int state = 0; state < kPhaseCount; ++state) {
        const LeavePhase phase = static_cast<LeavePhase>(state);
        for (int age = 0; age <= kMaxTrackedAgeS; ++age) {
            const double sourceMass = mass_[state][age];
            if (sourceMass <= 0.0) {
                continue;
            }
            const int nextAge = std::min(kMaxTrackedAgeS, age + dtS);
            const double exitP = ExitProbability(phase, nextAge, dtS, effective, config);
            predicted[state][nextAge] += sourceMass * (1.0 - exitP);
            const double exiting = sourceMass * exitP;

            if (phase == LeavePhase::kAtAnchor) {
                predicted[PhaseIndex(LeavePhase::kPreLeave)][0] += exiting;
            } else if (phase == LeavePhase::kPreLeave) {
                const double leaveShare = Clip01(config.preleave_exit_to_leaving);
                predicted[PhaseIndex(LeavePhase::kLeaving)][0] += exiting * leaveShare;
                predicted[PhaseIndex(LeavePhase::kAtAnchor)][0] += exiting * (1.0 - leaveShare);
            } else if (phase == LeavePhase::kLeaving) {
                const double outsideShare = effective.outside ? 0.99 : 0.02;
                predicted[PhaseIndex(LeavePhase::kOutside)][0] += exiting * outsideShare;
                predicted[PhaseIndex(LeavePhase::kPreLeave)][0] += exiting * (1.0 - outsideShare);
            } else {
                predicted[PhaseIndex(LeavePhase::kAtAnchor)][0] += exiting;
            }
        }
    }

    const auto likelihood = EmissionLikelihood(effective, config);
    double total = 0.0;
    for (int state = 0; state < kPhaseCount; ++state) {
        for (double &value : predicted[state]) {
            value *= likelihood[state];
            total += value;
        }
    }

    if (total <= kProbabilityFloor || !std::isfinite(total)) {
        Initialize(effective, tMs);
        return Summarize();
    }
    for (auto &stateMass : predicted) {
        for (double &value : stateMass) {
            value /= total;
        }
    }
    mass_ = std::move(predicted);
    return Summarize();
}

LeaveHsmmResult LeaveHsmm::Summarize() const
{
    LeaveHsmmResult result;
    for (int state = 0; state < kPhaseCount; ++state) {
        double sum = 0.0;
        for (double value : mass_[state]) {
            sum += value;
        }
        result.probability[state] = sum;
    }
    const auto best = std::max_element(result.probability.begin(), result.probability.end());
    result.phase = static_cast<LeavePhase>(std::distance(result.probability.begin(), best));
    return result;
}

}  // namespace commute_sa
