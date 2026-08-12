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
    theta.focus_side = "company";
    theta.lead_min_s = 90.0;
    theta.lead_max_s = 240.0;
    theta.arm_delay_s = 20.0;
    theta.min_evidence = 2;
    theta.enter_leave = 0.5;

    SceneEngine engine(anchors, theta);

    TickFeatures f;
    f.t_ms = 1754388000000LL;  // afternoon-ish local
    f.has_gps = true;
    f.lat = anchors.company.lat;
    f.lon = anchors.company.lon;
    f.acc = 10.0;
    f.walking = false;
    auto d0 = engine.Step(f);
    std::cout << "still scene=" << SceneToString(d0.scene) << " score_c=" << d0.score_company
              << " eta=" << d0.eta_leave_s << " push=" << d0.should_service << "\n";
    if (d0.should_service || d0.service_intent == "DEPARTURE_NOTIFICATION") {
        std::cerr << "FAIL: home departure must be disabled in company focus\n";
        return 1;
    }

    // Walk outward from company.
    f.walking = true;
    f.has_walk_started = true;
    f.walk_started_at_ms = f.t_ms - 30000;
    bool pushed = false;
    for (int i = 0; i < 8; ++i) {
        f.t_ms += 5000;
        f.lat = anchors.company.lat + 0.00008 * (i + 1);
        f.lon = anchors.company.lon;
        auto d = engine.Step(f);
        std::cout << "i=" << i << " scene=" << SceneToString(d.scene)
                  << " rel=" << RelationToString(d.company_relation) << " dist=" << d.dist_company_m
                  << " score=" << d.score_company << " hits=" << d.hits_company << " eta=" << d.eta_leave_s
                  << " lead_ok=" << d.lead_gate_ok << " push=" << d.should_service
                  << " block=" << d.push_block_reason << " intent=" << d.service_intent << "\n";
        if (d.should_service) {
            pushed = true;
            if (d.service_intent != "LEAVE_COMPANY_NOTIFICATION") {
                std::cerr << "FAIL: expected LEAVE_COMPANY_NOTIFICATION\n";
                return 1;
            }
            if (d.company_relation == Relation::kOutside) {
                std::cerr << "FAIL: pushed while OUTSIDE company\n";
                return 1;
            }
        }
    }

    f.t_ms += 5000;
    f.lat = anchors.company.lat + 0.002;
    auto dout = engine.Step(f);
    std::cout << "outside scene=" << SceneToString(dout.scene)
              << " rel=" << RelationToString(dout.company_relation) << " push=" << dout.should_service
              << " block=" << dout.push_block_reason << "\n";
    if (dout.should_service) {
        std::cerr << "FAIL: push after outside company\n";
        return 1;
    }

    // Near home with walking must NOT produce DEPARTURE when focus=company.
    SceneEngine eng2(anchors, theta);
    TickFeatures fh = f;
    fh.t_ms += 10000;
    fh.lat = anchors.home.lat + 0.0002;
    fh.lon = anchors.home.lon;
    fh.walking = true;
    fh.has_walk_started = true;
    fh.walk_started_at_ms = fh.t_ms - 30000;
    for (int i = 0; i < 6; ++i) {
        fh.t_ms += 5000;
        fh.lat = anchors.home.lat + 0.00008 * (i + 1);
        auto d = eng2.Step(fh);
        if (d.should_service || d.service_intent == "DEPARTURE_NOTIFICATION" ||
            d.scene == Scene::kLeavingHome) {
            std::cerr << "FAIL: home leave active under focus_side=company scene=" << SceneToString(d.scene)
                      << " intent=" << d.service_intent << "\n";
            return 1;
        }
    }

    // A validated high-level policy can trigger on PRE_LEAVE using radio+motion
    // while GPS still places the user inside the company fence.
    SceneEngine policyEngine(anchors, theta);
    PersonalizationPolicy policy;
    BuildPolicyTemplate("wifi_first_preleave", &policy, nullptr);
    policy.probability_threshold = 0.35;
    policy.min_duration_s = 0.0;
    policyEngine.SetPersonalizationPolicy(policy);
    TickFeatures fp;
    fp.t_ms = f.t_ms + 20000;
    fp.has_gps = true;
    fp.lat = anchors.company.lat;
    fp.lon = anchors.company.lon;
    fp.acc = 20.0;
    policyEngine.Step(fp);  // establish AT_COMPANY
    fp.walking = true;
    fp.has_walk_started = true;
    fp.walk_started_at_ms = fp.t_ms - 30000;
    fp.wifi_company_detach = true;
    fp.cell_leave_company = true;
    fp.pdr_net_out_company_m = 5.0;
    fp.wifi_jaccard_company = 0.0;
    bool policyPushed = false;
    for (int i = 0; i < 12 && !policyPushed; ++i) {
        fp.t_ms += 5000;
        const auto dp = policyEngine.Step(fp);
        policyPushed = dp.should_service;
        if (policyPushed && (dp.policy_template != "wifi_first_preleave" ||
            dp.hsmm_preleave_company < policy.probability_threshold)) {
            std::cerr << "FAIL: policy push did not come from PRE_LEAVE gate\n";
            return 1;
        }
    }
    if (!policyPushed) {
        std::cerr << "FAIL: wifi_first_preleave policy never pushed\n";
        return 1;
    }

    BaselineRuntime::GetInstance().Init("D:/huawei/commute_scene_baseline/config/anchors.json",
        "D:/huawei/commute_scene_baseline/config/theta_default.json");
    BaselineRuntime::GetInstance().OnWalkingStarted(f.t_ms - 30000);
    auto d2 = BaselineRuntime::GetInstance().OnTick(f.t_ms, true, anchors.company.lat + 0.0004, anchors.company.lon,
        10.0, true);
    std::cout << "runtime scene=" << SceneToString(d2.scene) << " push=" << d2.should_service
              << " eta=" << d2.eta_leave_s << "\n";

    const double sep = HaversineM(anchors.home.lat, anchors.home.lon, anchors.company.lat, anchors.company.lon);
    if (sep < 1000.0) {
        std::cerr << "home/company too close\n";
        return 1;
    }
    std::cout << "home-company sep_m=" << sep << " pushed_in_walk=" << (pushed ? 1 : 0)
              << " policy_preleave_push=" << (policyPushed ? 1 : 0) << "\nok\n";
    return 0;
}
