#include "commute_sa/baro_evidence.h"

#include <iostream>

using namespace commute_sa;

int main()
{
    BaroEvidence baro;
    int64_t t = 1000000;
    for (int i = 0; i < 70; ++i) baro.Observe(t + i * 100, 1000.0);
    auto base = baro.Evaluate(t + 7000, true, 12.0);
    if (!base.baseline_ready || !base.stable_platform) return 1;
    for (int i = 0; i < 70; ++i) baro.Observe(t + 8000 + i * 100, 1001.0); // about 8.4 m
    auto shortDrop = baro.Evaluate(t + 15000, false, 12.0);
    if (shortDrop.lower_platform) return 2;
    for (int i = 0; i < 70; ++i) baro.Observe(t + 16000 + i * 100, 1002.0); // about 16.8 m
    auto fullDrop = baro.Evaluate(t + 23000, false, 12.0);
    if (!fullDrop.lower_platform) return 3;
    std::cout << "baro ok descent_m=" << fullDrop.descent_m << "\n";
    return 0;
}
