#include "commute_sa/context_engine.h"
#include "commute_sa/context_template.h"
#include "commute_sa/product_store.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    using namespace commute_sa;
    ContextAbsenceClock clock;
    Check(clock.Step(1000, false, true, false, 10) == 0, "no trigger");
    Check(clock.Step(2000, true, true, false, 10) == 0, "trigger is neutral");
    Check(std::abs(clock.Step(7000, true, true, false, 10) - .5) < 1e-9, "valid interval ramp");
    Check(clock.Step(12000, true, false, false, 10) == 0, "missing is neutral");
    Check(std::abs(clock.Step(17000, true, true, false, 10) - .5) < 1e-9, "missing interval excluded");
    Check(clock.Step(22000, true, true, false, 10) == 1, "ramp saturation");
    Check(clock.Step(27000, true, true, true, 10) == 0, "expected clears absence");
    Check(clock.Step(32000, true, true, false, 10) == 0, "satisfaction latched");
    Check(clock.Step(70000, true, true, false, 10) == 0, "long gap resets");
    Check(clock.Step(1000, true, true, false, 10) == 0, "clock reversal resets");

    LeaveObservation o;
    o.context_available = true;
    o.context_negative_strength = 1.2;
    o.context_return_strength = .6;
    o.context_absence = 1;
    ComposeContextEvidence(o);
    Check(std::abs(o.context_scores[2] + 2.4) < 1e-9, "negative suppresses leaving");
    const auto negative = o.context_scores;
    o.negative_pattern_match = 1;
    ComposeContextEvidence(o);
    Check(o.context_scores == negative, "overlapping negative max, not sum");
    o.cancel_sequence_match = 1;
    ComposeContextEvidence(o);
    Check(std::abs(o.context_scores[2] + 1.2) < 1e-9, "return owns negative, no double count");
    o.context_negative_strength = o.context_return_strength = 0;
    ComposeContextEvidence(o);
    Check(o.context_scores[2] == 0, "zero strength disables");
    o.context_positive_strength = 2.4;
    o.sequence_ready = 1;
    o.sequence_complete = 1;
    o.sequence_progress = 1;
    ComposeContextEvidence(o);
    Check(std::abs(o.context_scores[2] - 5.76) < 1e-9, "ready replaces complete/progress");

    // HSMM consumes context exactly once and ignores legacy template fields.
    LeaveHsmm a, b;
    LeaveHsmmConfig cfg;
    LeaveObservation copy = o;
    copy.sequence_available = true;
    copy.sequence_reliability = 1;
    for (int t = 1000; t <= 60000; t += 1000) {
        auto x = a.Step(o, t, cfg), y = b.Step(copy, t, cfg);
        Check(x.probability == y.probability, "context plus legacy double count");
        double sum = 0;
        for (double p : x.probability) { Check(std::isfinite(p) && p >= 0, "invalid posterior"); sum += p; }
        Check(std::abs(sum - 1) < 1e-9, "posterior normalization");
    }
    // Persisted profile -> online adapter -> vector. No training labels involved.
    const auto dir = std::filesystem::temp_directory_path() / ("commute_context_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    Check(ProductStore::GetInstance().Init(dir.string()), "fixture store init");
    std::ofstream profile(dir / "active_context_template.json");
    profile << R"({"template_name":"test","side":"company","anchor_id":"company_001",
      "applicability":"always","positive_sequence":"walking,wifi_detach","strength":0.2,
      "context_engine":true,"positive_strength":0.2,"negative_strength":1.2,
      "return_strength":0.6,"absence_trigger":"walking,wifi_detach",
      "absence_expected":"baro_descending","absence_wait_s":10})";
    profile.close();
    ReloadActiveContextTemplateRuntime();
    auto tick = [](int64_t t, bool available, double descending) {
        LeaveObservation obs;
        obs.walking = obs.wifi_detach = 1;
        obs.baro_available = available;
        obs.baro_descending = descending;
        Check(ApplyActiveContextTemplateObservation("company", "company_001", t, &obs), "online apply");
        return obs;
    };
    Check(tick(1000, true, 0).context_absence == 0, "ordered trigger first stage");
    Check(tick(6000, true, 0).context_absence == 0, "ordered trigger second stage");
    Check(tick(11000, true, 0).context_absence == .5, "online ramp");
    Check(tick(16000, false, 0).context_absence == 0, "online missing neutral");
    Check(tick(21000, true, 0).context_absence == .5, "online excludes missing interval");
    Check(tick(26000, true, 1).context_absence == 0, "online expected clears");
    Check(tick(31000, true, 0).context_absence == 0, "online expected stays cleared");
    Check(tick(70000, true, 0).context_absence == 0, "online gap restarts trigger");
    // Fixture is deliberately left in the system temp directory for inspection.
    std::cout << "PASS context ramp, missing, reset, dedup, zero, HSMM single fusion\n";
}
