#include "commute_sa/anchors.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/scene_engine.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace commute_sa;

    AnchorSet anchors = DefaultAnchors();
    LoadAnchorsFromFile("D:/huawei/commute_scene_baseline/config/anchors.json", &anchors, nullptr);
    Theta theta = DefaultTheta();
    LoadThetaFromFile("D:/huawei/commute_scene_baseline/config/theta_default.json", &theta, nullptr);
    // Predictable local morning hour for time prior (optional).
    theta.lead_min_s = 90.0;
    theta.lead_max_s = 240.0;
    theta.arm_delay_s = 20.0;
    theta.min_evidence = 2;
    theta.enter_leave = 0.5;

    SceneEngine engine(anchors, theta);

    TickFeatures f;
    f.t_ms = 1754352840000LL;
    f.has_gps = true;
    f.lat = anchors.home.lat;
    f.lon = anchors.home.lon;
    f.acc = 10.0;
    f.walking = false;
    auto d0 = engine.Step(f);
    std::cout << "still scene=" << SceneToString(d0.scene) << " score_h=" << d0.score_home
              << " eta=" << d0.eta_leave_s << " push=" << d0.should_service << "\n";

    // Walk outward over several ticks so ETA shrinks into lead_max window.
    f.walking = true;
    f.has_walk_started = true;
    f.walk_started_at_ms = f.t_ms - 30000;
    bool pushed = false;
    for (int i = 0; i < 8; ++i) {
        f.t_ms += 5000;
        // ~12m further each step north
        f.lat = anchors.home.lat + 0.00008 * (i + 1);
        f.lon = anchors.home.lon;
        auto d = engine.Step(f);
        std::cout << "i=" << i << " scene=" << SceneToString(d.scene) << " rel=" << RelationToString(d.home_relation)
                  << " dist=" << d.dist_home_m << " score=" << d.score_home << " hits=" << d.hits_home
                  << " eta=" << d.eta_leave_s << " lead_ok=" << d.lead_gate_ok << " push=" << d.should_service
                  << " block=" << d.push_block_reason << " intent=" << d.service_intent << "\n";
        if (d.should_service) {
            pushed = true;
            if (d.home_relation == Relation::kOutside) {
                std::cerr << "FAIL: pushed while OUTSIDE\n";
                return 1;
            }
        }
    }

    // Far outside: must not push again
    f.t_ms += 5000;
    f.lat = anchors.home.lat + 0.002;
    auto dout = engine.Step(f);
    std::cout << "outside scene=" << SceneToString(dout.scene) << " rel=" << RelationToString(dout.home_relation)
              << " push=" << dout.should_service << " block=" << dout.push_block_reason << "\n";
    if (dout.should_service) {
        std::cerr << "FAIL: push after outside\n";
        return 1;
    }

    BaselineRuntime::GetInstance().Init("D:/huawei/commute_scene_baseline/config/anchors.json",
        "D:/huawei/commute_scene_baseline/config/theta_default.json");
    BaselineRuntime::GetInstance().OnWalkingStarted(f.t_ms - 30000);
    auto d2 = BaselineRuntime::GetInstance().OnTick(f.t_ms, true, anchors.home.lat + 0.0004, anchors.home.lon, 10.0,
        true);
    std::cout << "runtime scene=" << SceneToString(d2.scene) << " push=" << d2.should_service
              << " eta=" << d2.eta_leave_s << "\n";

    const double sep = HaversineM(anchors.home.lat, anchors.home.lon, anchors.company.lat, anchors.company.lon);
    if (sep < 1000.0) {
        std::cerr << "home/company too close\n";
        return 1;
    }
    std::cout << "home-company sep_m=" << sep << " pushed_in_walk=" << (pushed ? 1 : 0) << "\nok\n";
    return 0;
}
