#pragma once

#include <cstdint>
#include <deque>

namespace commute_sa {

struct BaroSnapshot {
    bool available = false;
    bool baseline_ready = false;
    double pressure_hpa = 0.0;
    double descent_m = 0.0;
    double descending = 0.0;
    double ascending = 0.0;
    bool stable_platform = false;
    bool lower_platform = false;
    bool vertical_closure = false;
};

class BaroEvidence {
public:
    void Observe(int64_t tMs, double pressureHpa);
    BaroSnapshot Evaluate(int64_t tMs, bool workplaceReady, double minDescentM = 12.0);
    void Reset();

private:
    struct Sample { int64_t t_ms; double pressure_hpa; };
    std::deque<Sample> samples_;
    bool baseline_ready_ = false;
    double baseline_hpa_ = 0.0;
    double prev_pressure_hpa_ = 0.0;
    double max_descent_m_ = 0.0;
};

}  // namespace commute_sa
