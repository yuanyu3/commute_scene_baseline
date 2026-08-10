#pragma once

#include "commute_sa/geo.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace commute_sa {

struct WifiApSample {
    std::string bssid;
    int32_t rssi = -127;
};

struct WifiScanSample {
    int64_t t_ms = 0;
    std::vector<WifiApSample> aps;
};

struct CellSample {
    int64_t t_ms = 0;
    int64_t cell_id = 0;
    int32_t rssi = -127;
};

struct BleSample {
    int64_t t_ms = 0;
    std::string mac;
    int32_t rssi = -127;
};

struct RadioEvidenceConfig {
    /** Min RSSI to count an AP into sets. */
    int32_t rssi_min = -85;
    /** Stronger threshold for "still attached" checks. */
    int32_t rssi_strong = -75;
    /** Learn soft dwell fingerprint after this many ms INSIDE. */
    int64_t dwell_learn_ms = 180000;
    /** Max BSSIDs kept in soft dwell set. */
    int dwell_set_size = 8;
    /** Min scans contributing to a dwell BSSID. */
    int dwell_min_count = 3;
    /** Jaccard(current, dwell) below this → detach (when dwell set ready). */
    double jaccard_detach = 0.30;
    /** Jaccard(current, scan ~baseline_ago) below this → churn detach (no dwell yet). */
    double jaccard_churn = 0.35;
    /** Jaccard(current, dwell) above this → attach (when dwell set ready). */
    double jaccard_attach = 0.55;
    /** Strong-AP count rise vs baseline → attach. */
    int attach_n_strong_delta = 5;
    int attach_n_strong_abs = 8;
    /** Look-back for temporal baseline scan. */
    int64_t churn_baseline_ms = 120000;
    /** Dwell BSSID median RSSI drop (dB) to count as detach. */
    double rssi_drop_db = 12.0;
    /** Cell must be stable this long before a change counts as leave. */
    int64_t cell_stable_ms = 90000;
    /** Detach must hold this long (or N scans) before latching true. */
    int64_t detach_confirm_ms = 15000;
    int detach_confirm_scans = 2;
    /** Keep this many recent scans. */
    size_t history_cap = 40;
    /** Soft set older than this is discarded on load (default 7 days). */
    int64_t soft_persist_ttl_ms = 7LL * 24 * 3600 * 1000;
    /** Min interval between disk writes when soft set dirty. */
    int64_t soft_persist_min_interval_ms = 60000;
};

struct RadioDetachSnapshot {
    bool wifi_home_detach = false;
    bool wifi_company_detach = false;
    bool wifi_home_attach = false;
    bool wifi_company_attach = false;
    bool cell_leave_home = false;
    bool cell_leave_company = false;
    bool ble_home_detach = false;
    bool ble_company_detach = false;
    /** Continuous 0..1 radio leave evidence (max of home/company paths). */
    double s_radio = 0.0;
    double jaccard_home = 1.0;
    double jaccard_company = 1.0;
    double jaccard_churn = 1.0;
    bool home_dwell_ready = false;
    bool company_dwell_ready = false;
    int n_strong = 0;
    std::string reason;
};

/**
 * Online WiFi/CELL leave evidence with optional semi-persistent soft dwell sets.
 *
 * Soft sets are learned while GPS INSIDE, written to radio_soft.json, reloaded on Init.
 * They expire after soft_persist_ttl_ms — not a static a priori whitelist.
 */
class RadioEvidence {
public:
    void SetConfig(const RadioEvidenceConfig &cfg);
    RadioEvidenceConfig GetConfig() const;

    void OnWifiScan(const WifiScanSample &scan);
    void OnCellSample(const CellSample &cell);
    void OnBleSample(const BleSample &ble);

    /**
     * After SceneEngine Step: update soft fingerprints from INSIDE dwell.
     * relHome/relCompany use Relation enum.
     */
    void ObserveDwell(int64_t tMs, Relation relHome, Relation relCompany);

    /** Fill detach flags for the next / current tick features (mutates hysteresis). */
    RadioDetachSnapshot Evaluate(int64_t tMs);

    /** Debug JSON for tools / HAP (no latch mutation). */
    std::string DebugJson(int64_t tMs) const;

    /** Clear session histories / hysteresis; soft sets cleared unless keepSoft. */
    void Reset(bool keepSoft = false);

    /** Load semi-persistent soft sets from JSON; drops expired sides. */
    bool ImportSoftJson(const std::string &json, int64_t nowMs);

    /** Export ready soft sets (home/company) for disk. */
    std::string ExportSoftJson() const;

    /** True if soft set changed and should be flushed soon. */
    bool SoftDirty() const;
    void ClearSoftDirty();

    /** Whether enough time passed since last persist attempt. */
    bool SoftPersistDue(int64_t nowMs) const;
    void MarkSoftPersisted(int64_t nowMs);

private:
    struct DwellState {
        int64_t inside_since_ms = 0;
        int64_t inside_accum_ms = 0;
        int64_t last_inside_ms = 0;
        int64_t soft_updated_at_ms = 0;
        bool was_inside = false;
        bool from_persist = false;
        std::unordered_map<std::string, int> bssid_count;
        std::unordered_map<std::string, std::deque<int32_t>> bssid_rssi;
        std::unordered_set<std::string> soft_set;
        bool ready = false;
    };

    mutable std::mutex mutex_;
    RadioEvidenceConfig cfg_ {};
    std::deque<WifiScanSample> wifiHistory_;
    std::deque<CellSample> cellHistory_;
    std::deque<BleSample> bleHistory_;

    DwellState home_;
    DwellState company_;
    bool soft_dirty_ = false;
    int64_t last_soft_persist_ms_ = 0;

    int64_t stable_cell_id_ = 0;
    int64_t stable_cell_since_ms_ = 0;
    int64_t last_cell_id_ = 0;

    int home_detach_streak_ = 0;
    int company_detach_streak_ = 0;
    int64_t home_detach_since_ms_ = 0;
    int64_t company_detach_since_ms_ = 0;
    int home_attach_streak_ = 0;
    int company_attach_streak_ = 0;
    int64_t home_attach_since_ms_ = 0;
    int64_t company_attach_since_ms_ = 0;
    int cell_home_streak_ = 0;
    int cell_company_streak_ = 0;
    int ble_home_streak_ = 0;
    int ble_company_streak_ = 0;
    int64_t ble_home_since_ms_ = 0;
    int64_t ble_company_since_ms_ = 0;

    static double Jaccard(const std::unordered_set<std::string> &a, const std::unordered_set<std::string> &b);
    static std::unordered_set<std::string> StrongSet(const WifiScanSample &scan, int32_t rssiMin);
    static std::unordered_set<std::string> BleStrongSet(const std::deque<BleSample> &hist, int64_t tMs, int64_t windowMs,
        int32_t rssiMin);
    void TrimHistoryLocked();
    void UpdateDwellLocked(DwellState *st, int64_t tMs, bool inside, const WifiScanSample *latestWifi);
    bool RebuildSoftSetLocked(DwellState *st, int64_t tMs);
    void ApplySoftSideLocked(DwellState *st, const std::vector<std::string> &bssids,
        const std::unordered_map<std::string, int32_t> &rssiMed, int64_t updatedAtMs, int64_t nowMs);
    bool DetachAgainstDwellLocked(const DwellState &st, const WifiScanSample &cur, double *jaccardOut,
        std::string *why) const;
    bool TemporalChurnLocked(int64_t tMs, const WifiScanSample &cur, double *jaccardOut, std::string *why) const;
    bool TemporalBleChurnLocked(int64_t tMs, double *jaccardOut, std::string *why) const;
    bool CellLeaveLocked(int64_t tMs) const;
    bool NStrongSurgeLocked(int64_t tMs, const WifiScanSample &cur) const;
    bool AttachAgainstDwellLocked(const DwellState &st, const WifiScanSample &cur, double jaccard) const;
    static std::string SideToJson(const DwellState &st);
};

}  // namespace commute_sa
