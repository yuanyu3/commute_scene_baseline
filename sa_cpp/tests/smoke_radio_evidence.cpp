#include "commute_sa/radio_evidence.h"

#include <iostream>
#include <string>

namespace {

commute_sa::WifiScanSample MakeScan(int64_t tMs, const std::initializer_list<std::pair<const char *, int>> &aps)
{
    commute_sa::WifiScanSample s;
    s.t_ms = tMs;
    for (const auto &p : aps) {
        commute_sa::WifiApSample ap;
        ap.bssid = p.first;
        ap.rssi = p.second;
        s.aps.push_back(ap);
    }
    return s;
}

}  // namespace

int main()
{
    using namespace commute_sa;

    RadioEvidence radio;
    RadioEvidenceConfig cfg;
    cfg.dwell_learn_ms = 5000;
    cfg.dwell_min_count = 2;
    cfg.detach_confirm_scans = 2;
    cfg.detach_confirm_ms = 1000;
    cfg.jaccard_detach = 0.30;
    cfg.jaccard_churn = 0.35;
    cfg.churn_baseline_ms = 10000;
    cfg.cell_stable_ms = 5000;
    radio.SetConfig(cfg);

    const int64_t t0 = 1'700'000'000'000LL;

    // Learn soft home dwell while INSIDE.
    for (int i = 0; i < 6; ++i) {
        const int64_t t = t0 + i * 2000;
        radio.OnWifiScan(MakeScan(t, {{"aa:bb:cc:dd:ee:01", -55}, {"aa:bb:cc:dd:ee:02", -60},
                                         {"aa:bb:cc:dd:ee:03", -65}}));
        radio.ObserveDwell(t, Relation::kInside, Relation::kOutside);
    }

    auto mid = radio.Evaluate(t0 + 12000);
    std::cout << "after_dwell ready=" << mid.home_dwell_ready << " detach=" << mid.wifi_home_detach
              << " jac=" << mid.jaccard_home << " reason=" << mid.reason << "\n";
    if (!mid.home_dwell_ready) {
        std::cerr << "FAIL: home dwell should be ready\n";
        return 1;
    }
    if (mid.wifi_home_detach) {
        std::cerr << "FAIL: should not detach while still on dwell set\n";
        return 1;
    }

    // Completely different BSSIDS → detach after confirm scans.
    bool detached = false;
    for (int i = 0; i < 4; ++i) {
        const int64_t t = t0 + 20000 + i * 3000;
        radio.OnWifiScan(MakeScan(t, {{"11:22:33:44:55:01", -50}, {"11:22:33:44:55:02", -52},
                                         {"11:22:33:44:55:03", -54}}));
        auto snap = radio.Evaluate(t);
        std::cout << "leave_i=" << i << " detach=" << snap.wifi_home_detach << " jac=" << snap.jaccard_home
                  << " s_radio=" << snap.s_radio << " reason=" << snap.reason << "\n";
        if (snap.wifi_home_detach) {
            detached = true;
            break;
        }
    }
    if (!detached) {
        std::cerr << "FAIL: expected wifi_home_detach after environment change\n";
        return 1;
    }

    // Cell leave: stable then change.
    RadioEvidence radio2;
    radio2.SetConfig(cfg);
    radio2.OnCellSample({t0, 1001, -80});
    radio2.OnCellSample({t0 + 6000, 1001, -80});
    auto c0 = radio2.Evaluate(t0 + 6000);
    if (c0.cell_leave_home) {
        std::cerr << "FAIL: no leave while stable\n";
        return 1;
    }
    radio2.OnCellSample({t0 + 7000, 2002, -85});
    auto c1 = radio2.Evaluate(t0 + 7000);
    radio2.OnCellSample({t0 + 8000, 2002, -85});
    auto c2 = radio2.Evaluate(t0 + 8000);
    std::cout << "cell leave streak: " << c1.cell_leave_home << " -> " << c2.cell_leave_home
              << " reason=" << c2.reason << "\n";
    if (!c2.cell_leave_home) {
        std::cerr << "FAIL: expected cell_leave_home after stable cell change\n";
        return 1;
    }

    // Temporal churn before dwell ready.
    RadioEvidence radio3;
    radio3.SetConfig(cfg);
    radio3.OnWifiScan(MakeScan(t0, {{"aa:01", -50}, {"aa:02", -52}, {"aa:03", -54}}));
    radio3.OnWifiScan(MakeScan(t0 + 12000, {{"bb:01", -50}, {"bb:02", -52}, {"bb:03", -54}}));
    auto churn1 = radio3.Evaluate(t0 + 12000);
    auto churn2 = radio3.Evaluate(t0 + 12500);
    std::cout << "churn jac=" << churn2.jaccard_churn << " detach=" << churn2.wifi_home_detach
              << " ready=" << churn2.home_dwell_ready << "\n";
    if (churn2.home_dwell_ready) {
        std::cerr << "FAIL: dwell should not be ready without ObserveDwell\n";
        return 1;
    }
    if (!churn1.wifi_home_detach && !churn2.wifi_home_detach) {
        std::cerr << "FAIL: expected temporal churn detach\n";
        return 1;
    }

    // Semi-persistent soft set: export → reset → import → detach without re-learn delay.
    RadioEvidence radio4;
    radio4.SetConfig(cfg);
    for (int i = 0; i < 6; ++i) {
        const int64_t t = t0 + i * 2000;
        radio4.OnWifiScan(MakeScan(t, {{"cc:01", -50}, {"cc:02", -52}, {"cc:03", -55}}));
        radio4.ObserveDwell(t, Relation::kOutside, Relation::kInside);
    }
    if (!radio4.Evaluate(t0 + 12000).company_dwell_ready) {
        std::cerr << "FAIL: company dwell should be ready before export\n";
        return 1;
    }
    const std::string softJson = radio4.ExportSoftJson();
    std::cout << "soft_json=" << softJson << "\n";
    RadioEvidence radio5;
    radio5.SetConfig(cfg);
    radio5.Reset(false);
    if (!radio5.ImportSoftJson(softJson, t0 + 86400000LL)) {
        std::cerr << "FAIL: ImportSoftJson\n";
        return 1;
    }
    auto loaded = radio5.Evaluate(t0 + 86400000LL);
    if (!loaded.company_dwell_ready) {
        std::cerr << "FAIL: company dwell not ready after import\n";
        return 1;
    }
    radio5.OnWifiScan(MakeScan(t0 + 86401000LL, {{"dd:01", -50}, {"dd:02", -52}, {"dd:03", -54}}));
    auto d1 = radio5.Evaluate(t0 + 86401000LL);
    auto d2 = radio5.Evaluate(t0 + 86402000LL);
    std::cout << "persist detach " << d1.wifi_company_detach << " -> " << d2.wifi_company_detach
              << " from_persist dbg=" << radio5.DebugJson(0) << "\n";
    if (!d1.wifi_company_detach && !d2.wifi_company_detach) {
        std::cerr << "FAIL: expected company detach from persisted soft set\n";
        return 1;
    }

    // Expired snapshot must be ignored.
    RadioEvidence radio6;
    radio6.SetConfig(cfg);
    RadioEvidenceConfig cfgExp = cfg;
    cfgExp.soft_persist_ttl_ms = 1000;
    radio6.SetConfig(cfgExp);
    if (radio6.ImportSoftJson(softJson, t0 + 86400000LL * 30)) {
        // Import returns true only if any side applied; expired should yield false/not ready
    }
    if (radio6.Evaluate(t0 + 86400000LL * 30).company_dwell_ready) {
        std::cerr << "FAIL: expired soft set should not be ready\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
