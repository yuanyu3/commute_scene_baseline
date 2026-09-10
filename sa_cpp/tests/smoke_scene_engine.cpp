#include "commute_sa/anchors.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/scene_engine.h"
#include "commute_sa/types.h"

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

    // Company gate: GPS fence is auxiliary; source_type 2=inside, 2→1=outside the door.
    SceneEngine srcEngine(anchors, theta);
    TickFeatures fs;
    fs.t_ms = 1754388000000LL;
    fs.has_gps = true;
    fs.lat = anchors.company.lat + 0.002;  // GPS fence would be OUTSIDE
    fs.lon = anchors.company.lon;
    fs.acc = 80.0;
    fs.gps_source_type = kLocationSourceIndoorNetwork;
    auto dIn = srcEngine.Step(fs);
    if (dIn.company_relation != Relation::kInside) {
        std::cerr << "FAIL: source_type=2 near company must be INSIDE, got "
                  << RelationToString(dIn.company_relation) << "\n";
        return 1;
    }
    fs.t_ms += 5000;
    fs.lat = anchors.company.lat;  // GPS fence would still be INSIDE
    fs.gps_source_type = kLocationSourceOutdoorGnss;
    auto dGate = srcEngine.Step(fs);
    if (dGate.company_relation != Relation::kOutside) {
        std::cerr << "FAIL: source_type 2→1 must be OUTSIDE company gate, got "
                  << RelationToString(dGate.company_relation) << "\n";
        return 1;
    }
    if (dGate.should_service) {
        std::cerr << "FAIL: must not push after GNSS confirms outside the gate\n";
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

    // Indoor HSMM leave: walking + radio detach at company, source_type=2.
    // Engine ignores policy templates; push is P(LEAVING) >= enter_leave.
    // Legacy timing fields may still be present in theta.json, but neither a
    // just-started walk nor an ETA above lead_max may block a valid candidate.
    Theta noTimingGateTheta = theta;
    noTimingGateTheta.arm_delay_s = 3600.0;
    noTimingGateTheta.lead_max_s = 1.0;
    SceneEngine hsmmEngine(anchors, noTimingGateTheta);
    TickFeatures fp;
    fp.t_ms = f.t_ms + 20000;
    fp.has_gps = true;
    fp.lat = anchors.company.lat;
    fp.lon = anchors.company.lon;
    fp.acc = 20.0;
    fp.gps_source_type = kLocationSourceIndoorNetwork;
    hsmmEngine.Step(fp);
    fp.walking = true;
    fp.has_walk_started = true;
    fp.walk_started_at_ms = fp.t_ms;
    fp.wifi_company_detach = true;
    fp.cell_leave_company = true;
    fp.pdr_net_out_company_m = 20.0;
    fp.wifi_jaccard_company = 0.0;
    bool hsmmPushed = false;
    TickDecision lastIndoor;
    for (int i = 0; i < 36 && !hsmmPushed; ++i) {
        fp.t_ms += 5000;
        lastIndoor = hsmmEngine.Step(fp);
        hsmmPushed = lastIndoor.should_service;
        if (hsmmPushed && (lastIndoor.policy_template != "confirmed_leaving" ||
            lastIndoor.service_intent != "LEAVE_COMPANY_NOTIFICATION")) {
            std::cerr << "FAIL: indoor HSMM push must be confirmed_leaving company leave\n";
            return 1;
        }
        if (hsmmPushed && lastIndoor.company_relation == Relation::kOutside) {
            std::cerr << "FAIL: pushed while OUTSIDE company\n";
            return 1;
        }
    }
    if (!hsmmPushed) {
        std::cerr << "FAIL: indoor HSMM leave never pushed score=" << lastIndoor.score_company
                  << " rel=" << RelationToString(lastIndoor.company_relation)
                  << " phase=" << lastIndoor.hsmm_phase_company
                  << " pre=" << lastIndoor.hsmm_preleave_company
                  << " hits=" << lastIndoor.hits_company
                  << " block=" << lastIndoor.push_block_reason
                  << " obs_in=" << lastIndoor.hsmm_obs_company.inside
                  << " wifi=" << lastIndoor.hsmm_obs_company.wifi_detach
                  << " walk=" << lastIndoor.hsmm_obs_company.walking
                  << " pdr=" << lastIndoor.hsmm_obs_company.pdr_outbound << "\n";
        return 1;
    }

    // A zero weight disables the complete decision path for that sensor. Strong raw
    // values must not survive through evidence hits, HSMM transitions, or attach gates.
    Theta zeroTheta = theta;
    zeroTheta.w_walk = 0.0;
    zeroTheta.w_pdr = 0.0;
    zeroTheta.w_geo = 0.0;
    zeroTheta.w_wifi = 0.0;
    zeroTheta.w_cell = 0.0;
    zeroTheta.w_ble = 0.0;
    zeroTheta.w_time = 0.0;
    zeroTheta.w_baro = 0.0;
    SceneEngine zeroEngine(anchors, zeroTheta);
    TickFeatures fz = fp;
    fz.t_ms += 300000;
    fz.wifi_company_attach = true;
    fz.wifi_company_detach = true;
    fz.cell_leave_company = true;
    fz.ble_company_detach = true;
    fz.baro_available = true;
    fz.baro_baseline_ready = true;
    fz.baro_descent_m = 40.0;
    fz.baro_descending = true;
    fz.baro_stable_platform = true;
    fz.baro_lower_platform = true;
    const auto dz = zeroEngine.Step(fz);
    if (dz.hits_company != 0 || dz.hsmm_obs_company.walking > 0.0 ||
        dz.hsmm_obs_company.pdr_outbound > 0.0 || dz.hsmm_obs_company.wifi_detach > 0.0 ||
        dz.hsmm_obs_company.cell_detach > 0.0 || dz.hsmm_obs_company.ble_detach > 0.0 ||
        dz.hsmm_obs_company.time_prior > 0.0 || dz.hsmm_obs_company.baro_available ||
        dz.hsmm_obs_company.attached) {
        std::cerr << "FAIL: zero-weight sensor leaked into decision path"
                  << " hits=" << dz.hits_company
                  << " walk=" << dz.hsmm_obs_company.walking
                  << " pdr=" << dz.hsmm_obs_company.pdr_outbound
                  << " wifi=" << dz.hsmm_obs_company.wifi_detach
                  << " cell=" << dz.hsmm_obs_company.cell_detach
                  << " ble=" << dz.hsmm_obs_company.ble_detach
                  << " time=" << dz.hsmm_obs_company.time_prior
                  << " baro=" << dz.hsmm_obs_company.baro_available
                  << " attached=" << dz.hsmm_obs_company.attached << "\n";
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
              << " hsmm_indoor_push=" << (hsmmPushed ? 1 : 0) << "\nok\n";
    return 0;
}
