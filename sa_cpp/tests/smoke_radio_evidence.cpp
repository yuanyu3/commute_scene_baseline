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
    radio3.OnWifiScan(MakeScan(t0 + 12500, {{"bb:01", -50}, {"bb:02", -52}, {"bb:03", -54}}));
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
    radio5.OnWifiScan(MakeScan(t0 + 86402000LL, {{"dd:01", -50}, {"dd:02", -52}, {"dd:03", -54}}));
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

    // Long-lived Top-K site profile is independent from the expiring dwell set.
    RadioEvidence radio7;
    radio7.SetConfig(cfg);
    const std::string siteJson =
        "{\"company\":{\"wifi\":{\"bssids\":[\"ee:01\",\"ee:02\",\"ee:03\"]},"
        "\"cell\":{\"cell_ids\":[3003]}}}";
    if (!radio7.ImportCompanySiteJson(siteJson)) {
        std::cerr << "FAIL: ImportCompanySiteJson\n";
        return 1;
    }
    // Unrelated AP density must not dilute fingerprint recall.
    radio7.OnWifiScan(MakeScan(t0, {{"EE:01", -50}, {"ee:02", -55}, {"other:01", -60},
        {"other:02", -61}, {"other:03", -62}, {"other:04", -63}, {"other:05", -64}}));
    radio7.OnCellSample({t0, 3003, -80});
    auto site = radio7.Evaluate(t0);
    if (site.company_site_wifi_matches != 2 || site.company_site_wifi_coverage < 0.50 ||
        !site.company_site_cell_match || site.wifi_company_detach || site.cell_leave_company) {
        std::cerr << "FAIL: persisted company site fingerprint match\n";
        return 1;
    }

    // One low scan, even if Evaluate is called repeatedly, must not confirm a
    // detach. A third fresh low scan confirms it; two fresh high-recall scans
    // then confirm reattach. The intermediate band keeps the latched state.
    radio7.OnWifiScan(MakeScan(t0 + 1000, {{"other:01", -50}, {"other:02", -55}}));
    auto low1 = radio7.Evaluate(t0 + 1000);
    auto low1Repeated = radio7.Evaluate(t0 + 3000);
    if (low1.wifi_company_detach || low1Repeated.wifi_company_detach) {
        std::cerr << "FAIL: one physical scan must not confirm site detach\n";
        return 1;
    }
    radio7.OnWifiScan(MakeScan(t0 + 4000, {{"other:03", -50}, {"other:04", -55}}));
    auto low2 = radio7.Evaluate(t0 + 4000);
    if (low2.wifi_company_detach) {
        std::cerr << "FAIL: site detach should require three fresh low-recall scans\n";
        return 1;
    }
    radio7.OnWifiScan(MakeScan(t0 + 5000, {{"other:05", -50}, {"other:06", -55}}));
    auto low3 = radio7.Evaluate(t0 + 5000);
    if (!low3.wifi_company_detach || low3.company_site_wifi_coverage != 0.0) {
        std::cerr << "FAIL: three fresh low-recall scans should confirm site detach\n";
        return 1;
    }
    radio7.OnWifiScan(MakeScan(t0 + 6000, {{"ee:01", -55}, {"other:01", -60}}));
    auto middle = radio7.Evaluate(t0 + 6000);
    if (!middle.wifi_company_detach || middle.wifi_company_attach) {
        std::cerr << "FAIL: hysteresis band must retain detached state\n";
        return 1;
    }
    radio7.OnWifiScan(MakeScan(t0 + 7000, {{"ee:01", -55}, {"ee:02", -60}, {"other:01", -65}}));
    auto high1 = radio7.Evaluate(t0 + 7000);
    radio7.OnWifiScan(MakeScan(t0 + 8000, {{"ee:01", -55}, {"ee:03", -60}, {"other:02", -65}}));
    auto high2 = radio7.Evaluate(t0 + 8000);
    if (!high1.wifi_company_detach || high1.wifi_company_attach ||
        high2.wifi_company_detach || !high2.wifi_company_attach) {
        std::cerr << "FAIL: two fresh high-recall scans should confirm site reattach\n";
        return 1;
    }

    // Target-floor Cell whitelist: tolerate member handovers, smooth over a
    // recent window, and require fresh samples for detach/reattach.
    RadioEvidence radio8;
    RadioEvidenceConfig cfgCell = cfg;
    cfgCell.site_cell_window_ms = 3000;
    cfgCell.site_cell_min_samples = 3;
    cfgCell.site_cell_confirm_samples = 2;
    radio8.SetConfig(cfgCell);
    const std::string cellSiteJson =
        "{\"company\":{\"cell\":{\"cell_ids\":[3003,3004]}}}";
    if (!radio8.ImportCompanySiteJson(cellSiteJson)) {
        std::cerr << "FAIL: import floor Cell whitelist\n";
        return 1;
    }
    for (int i = 0; i < 3; ++i) {
        radio8.OnCellSample({t0 + i * 1000, i == 1 ? 3004 : 3003, -80});
        radio8.Evaluate(t0 + i * 1000);
    }
    bool cellDetached = false;
    for (int i = 0; i < 4; ++i) {
        radio8.OnCellSample({t0 + 10000 + i * 1000, 9001, -95});
        cellDetached = radio8.Evaluate(t0 + 10000 + i * 1000).cell_leave_company;
    }
    if (!cellDetached) {
        std::cerr << "FAIL: floor Cell whitelist should detach after a sustained non-match\n";
        return 1;
    }
    const auto repeatedCell = radio8.Evaluate(t0 + 14500);
    if (!repeatedCell.cell_leave_company) {
        std::cerr << "FAIL: repeated tick should retain, not clear, Cell detach\n";
        return 1;
    }
    bool cellReattached = false;
    for (int i = 0; i < 4; ++i) {
        radio8.OnCellSample({t0 + 20000 + i * 1000, i % 2 ? 3004 : 3003, -80});
        cellReattached = !radio8.Evaluate(t0 + 20000 + i * 1000).cell_leave_company;
    }
    if (!cellReattached) {
        std::cerr << "FAIL: floor Cell whitelist should reattach after sustained matches\n";
        return 1;
    }

    // BLE data may still be ingested, but default configuration must emit no
    // BLE evidence until stable beacons are explicitly supported/enabled.
    for (int i = 0; i < 20; ++i) {
        radio8.OnBleSample({t0 + i * 1000, "random:" + std::to_string(i), -50});
    }
    const auto bleDisabled = radio8.Evaluate(t0 + 20000);
    if (bleDisabled.ble_home_detach || bleDisabled.ble_company_detach) {
        std::cerr << "FAIL: BLE evidence should be disabled by default\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
