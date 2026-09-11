#pragma once
#include "commute_sa/leave_hsmm.h"
#include <algorithm>
#include <cmath>

namespace commute_sa {
// No probabilities or transition changes here: bounded per-state log evidence.
inline void ComposeContextEvidence(LeaveObservation &o)
{
    o.context_scores.fill(0.0);
    if (!o.context_available) return;
    const std::array<double, 4> progress {{-0.35, 0.65, 0.25, -0.30}};
    const std::array<double, 4> complete {{-1.0, 0.15, 1.20, -0.35}};
    const std::array<double, 4> negative {{0.80, 0.25, -1.0, -0.25}};
    auto unit = [](double x) { return std::isfinite(x) ? std::clamp(x, 0.0, 1.0) : 0.0; };
    // Cancellation already emits negative_pattern_match: do not count it twice.
    const double n = o.cancel_sequence_match >= 0.5 ? 0.0 :
        std::max(unit(o.negative_pattern_match), unit(o.context_absence));
    for (size_t s = 0; s < 4; ++s) {
        const double p = o.sequence_ready >= 0 ? unit(o.sequence_ready) * complete[s] :
            unit(o.sequence_progress) * progress[s] + unit(o.sequence_complete) * complete[s];
        o.context_scores[s] = std::clamp(2.0 * (o.context_positive_strength * p +
            o.context_negative_strength * n * negative[s] +
            o.context_return_strength * unit(o.cancel_sequence_match) * negative[s]), -6.0, 6.0);
    }
}

// Accumulate only consecutive valid observation time. A gap is not an absence.
struct ContextAbsenceClock {
    int64_t last_ms = 0;
    double valid_s = 0;
    bool previous_valid = false;
    bool satisfied = false;
    double Step(int64_t now, bool triggered, bool available, bool expected, double wait_s)
    {
        if (!triggered || (last_ms && (now < last_ms || now - last_ms > 30000))) {
            *this = {};
        }
        if (!triggered) return 0;
        if (available && expected) { satisfied = true; valid_s = 0; }
        if (!satisfied && available && previous_valid && last_ms && now > last_ms)
            valid_s += (now - last_ms) / 1000.0;
        last_ms = now;
        previous_valid = available;
        return available && !satisfied ? std::clamp(valid_s / std::max(1.0, wait_s), 0.0, 1.0) : 0.0;
    }
};
} // namespace commute_sa
