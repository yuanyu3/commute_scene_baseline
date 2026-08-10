#include "commute_sa/pdr_evidence.h"
#include "commute_sa/scene_engine.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace commute_sa;

    PdrEvidence pdr;
    const int64_t t0 = 1'700'000'000'000LL;

    // Indoor wander: long path, small net → low credit.
    pdr.OnWalkingStarted(t0);
    pdr.NoteWalkContext(Relation::kInside, Relation::kOutside);
    pdr.OnPdrPoint(t0 + 1, 0.0, 0.0);
    for (int i = 1; i <= 40; ++i) {
        // Oscillate ±2m
        const double x = (i % 2 == 0) ? 2.0 : -2.0;
        pdr.OnPdrPoint(t0 + i * 200, x, 0.0);
    }
    auto wander = pdr.Evaluate();
    std::cout << "wander net=" << wander.net_displacement_m << " path=" << wander.path_length_m
              << " credit_home=" << wander.pdr_net_out_home_m << " reason=" << wander.reason << "\n";
    if (wander.pdr_net_out_home_m > 5.0) {
        std::cerr << "FAIL: wander should not get large outbound credit\n";
        return 1;
    }

    // Straight outbound walk ~20m → should saturate sPdr for typical r_in.
    pdr.OnWalkingStarted(t0 + 100000);
    pdr.NoteWalkContext(Relation::kInside, Relation::kOutside);
    pdr.OnPdrPoint(t0 + 100001, 0.0, 0.0);
    for (int i = 1; i <= 20; ++i) {
        pdr.OnPdrPoint(t0 + 100001 + i * 300, static_cast<double>(i), 0.0);
    }
    auto leave = pdr.Evaluate();
    std::cout << "leave net=" << leave.net_displacement_m << " home=" << leave.pdr_net_out_home_m
              << " company=" << leave.pdr_net_out_company_m << " reason=" << leave.reason << "\n";
    if (std::fabs(leave.pdr_net_out_home_m - 20.0) > 0.2) {
        std::cerr << "FAIL: expected ~20m home net-out\n";
        return 1;
    }
    if (leave.pdr_net_out_company_m > 0.1) {
        std::cerr << "FAIL: company should not be tagged\n";
        return 1;
    }

    // ScoreLeaving integration: walk + pdr should reach enter with min_evidence=2.
    AnchorSet anchors = DefaultAnchors();
    anchors.company.lat = 40.05;
    anchors.company.lon = 116.17;
    anchors.company.r_in_m = 50.0;
    anchors.company.r_out_m = 90.0;
    Theta theta = DefaultTheta();
    theta.focus_side = "company";
    theta.enter_leave = 0.50;
    theta.min_evidence = 2;
    theta.arm_delay_s = 0.0;
    theta.w_time = 0.0;  // ignore clock prior for smoke
    SceneEngine eng(anchors, theta);

    TickFeatures feat;
    feat.t_ms = t0 + 200000;
    feat.has_gps = true;
    feat.lat = anchors.company.lat;
    feat.lon = anchors.company.lon;
    feat.acc = 5.0;
    feat.walking = true;
    feat.has_walk_started = true;
    feat.walk_started_at_ms = feat.t_ms - 30000;
    feat.pdr_net_out_company_m = 20.0;
    // Advance prev dist so geo can also hit on next step — first step just scores.
    auto d1 = eng.Step(feat);
    std::cout << "scoreC=" << d1.score_company << " hitsC=" << d1.hits_company
              << " scene=" << SceneToString(d1.scene) << "\n";
    if (d1.score_company < 0.30) {
        std::cerr << "FAIL: PDR+walk should contribute meaningful leave score\n";
        return 1;
    }
    // Second tick with growing GPS distance → geo hit too.
    feat.t_ms += 2000;
    feat.lat = anchors.company.lat + 0.00025;  // ~28m north
    auto d2 = eng.Step(feat);
    std::cout << "scoreC2=" << d2.score_company << " hitsC2=" << d2.hits_company
              << " scene=" << SceneToString(d2.scene) << " should=" << d2.should_service << "\n";
    if (d2.hits_company < 2) {
        std::cerr << "FAIL: expected >=2 evidence hits with walk+pdr(+geo)\n";
        return 1;
    }

    std::cout << "OK pdr_evidence smoke\n";
    return 0;
}
