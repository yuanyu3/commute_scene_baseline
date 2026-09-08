#include "commute_sa/baro_evidence.h"

#include <algorithm>
#include <cmath>

namespace commute_sa {

void BaroEvidence::Reset()
{
    samples_.clear(); baseline_ready_ = false; baseline_hpa_ = 0.0; prev_pressure_hpa_ = 0.0;
    max_descent_m_ = 0.0;
}

void BaroEvidence::Observe(int64_t tMs, double pressureHpa)
{
    if (pressureHpa < 850.0 || pressureHpa > 1100.0) return;
    samples_.push_back({tMs, pressureHpa});
    while (!samples_.empty() && tMs - samples_.front().t_ms > 20000) samples_.pop_front();
}

BaroSnapshot BaroEvidence::Evaluate(int64_t tMs, bool workplaceReady, double minDescentM)
{
    BaroSnapshot out;
    if (samples_.empty() || tMs - samples_.back().t_ms > 5000) return out;
    double sum = 0.0, lo = 1e9, hi = -1e9; int n = 0;
    for (auto it = samples_.rbegin(); it != samples_.rend() && tMs - it->t_ms <= 6000; ++it) {
        sum += it->pressure_hpa; lo = std::min(lo, it->pressure_hpa); hi = std::max(hi, it->pressure_hpa); ++n;
    }
    if (n == 0) return out;
    const double pressure = sum / n;
    const bool stable = n >= 5 && hi - lo <= 0.12;
    if (!baseline_ready_ && workplaceReady && stable) {
        baseline_hpa_ = pressure; baseline_ready_ = true; max_descent_m_ = 0.0;
    }
    const double descent = baseline_ready_ ? std::max(0.0, 44330.0 * (std::pow(pressure / baseline_hpa_, 0.1903) - 1.0)) : 0.0;
    max_descent_m_ = std::max(max_descent_m_, descent);
    out.available = true; out.baseline_ready = baseline_ready_; out.pressure_hpa = pressure;
    out.descent_m = descent; out.stable_platform = stable; out.lower_platform = stable && descent >= minDescentM;
    if (prev_pressure_hpa_ > 0.0) {
        out.descending = std::max(0.0, std::min(1.0, (pressure - prev_pressure_hpa_) / 0.18));
        out.ascending = std::max(0.0, std::min(1.0, (prev_pressure_hpa_ - pressure) / 0.18));
    }
    out.vertical_closure = baseline_ready_ && stable &&
        max_descent_m_ >= std::max(4.0, minDescentM) && descent <= 3.0;
    if (out.vertical_closure) max_descent_m_ = 0.0;
    prev_pressure_hpa_ = pressure;
    return out;
}

}  // namespace commute_sa
