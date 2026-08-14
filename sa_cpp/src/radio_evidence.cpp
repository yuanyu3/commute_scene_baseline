#include "commute_sa/radio_evidence.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace commute_sa {
namespace {

double Clip01(double x)
{
    if (x < 0.0) {
        return 0.0;
    }
    if (x > 1.0) {
        return 1.0;
    }
    return x;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

void RadioEvidence::SetConfig(const RadioEvidenceConfig &cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cfg_ = cfg;
}

RadioEvidenceConfig RadioEvidence::GetConfig() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cfg_;
}

void RadioEvidence::Reset(bool keepSoft)
{
    std::lock_guard<std::mutex> lock(mutex_);
    wifiHistory_.clear();
    cellHistory_.clear();
    bleHistory_.clear();
    const DwellState homeKeep = keepSoft ? home_ : DwellState {};
    const DwellState companyKeep = keepSoft ? company_ : DwellState {};
    home_ = DwellState {};
    company_ = DwellState {};
    if (keepSoft) {
        home_.soft_set = homeKeep.soft_set;
        home_.bssid_count = homeKeep.bssid_count;
        home_.bssid_rssi = homeKeep.bssid_rssi;
        home_.ready = homeKeep.ready;
        home_.soft_updated_at_ms = homeKeep.soft_updated_at_ms;
        home_.from_persist = homeKeep.from_persist;
        company_.soft_set = companyKeep.soft_set;
        company_.bssid_count = companyKeep.bssid_count;
        company_.bssid_rssi = companyKeep.bssid_rssi;
        company_.ready = companyKeep.ready;
        company_.soft_updated_at_ms = companyKeep.soft_updated_at_ms;
        company_.from_persist = companyKeep.from_persist;
        soft_dirty_ = false;
        last_soft_persist_ms_ = keepSoft ? last_soft_persist_ms_ : 0;
    } else {
        company_site_wifi_.clear();
        company_site_cells_.clear();
        company_site_wifi_detached_seen_ = false;
        soft_dirty_ = false;
        last_soft_persist_ms_ = 0;
    }
    stable_cell_id_ = 0;
    stable_cell_since_ms_ = 0;
    last_cell_id_ = 0;
    home_detach_streak_ = 0;
    company_detach_streak_ = 0;
    home_detach_since_ms_ = 0;
    company_detach_since_ms_ = 0;
    home_attach_streak_ = 0;
    company_attach_streak_ = 0;
    home_attach_since_ms_ = 0;
    company_attach_since_ms_ = 0;
    cell_home_streak_ = 0;
    cell_company_streak_ = 0;
    ble_home_streak_ = 0;
    ble_company_streak_ = 0;
    ble_home_since_ms_ = 0;
    ble_company_since_ms_ = 0;
}

void RadioEvidence::TrimHistoryLocked()
{
    while (wifiHistory_.size() > cfg_.history_cap) {
        wifiHistory_.pop_front();
    }
    while (cellHistory_.size() > cfg_.history_cap) {
        cellHistory_.pop_front();
    }
    while (bleHistory_.size() > cfg_.history_cap * 4) {
        bleHistory_.pop_front();
    }
}

std::unordered_set<std::string> RadioEvidence::StrongSet(const WifiScanSample &scan, int32_t rssiMin)
{
    std::unordered_set<std::string> out;
    for (const auto &ap : scan.aps) {
        if (ap.bssid.empty()) {
            continue;
        }
        if (ap.rssi >= rssiMin) {
            out.insert(Lower(ap.bssid));
        }
    }
    return out;
}

double RadioEvidence::Jaccard(const std::unordered_set<std::string> &a, const std::unordered_set<std::string> &b)
{
    if (a.empty() && b.empty()) {
        return 1.0;
    }
    if (a.empty() || b.empty()) {
        return 0.0;
    }
    size_t inter = 0;
    for (const auto &x : a) {
        if (b.count(x) != 0) {
            ++inter;
        }
    }
    const size_t uni = a.size() + b.size() - inter;
    if (uni == 0) {
        return 1.0;
    }
    return static_cast<double>(inter) / static_cast<double>(uni);
}

void RadioEvidence::OnWifiScan(const WifiScanSample &scan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (scan.t_ms <= 0 || scan.aps.empty()) {
        return;
    }
    wifiHistory_.push_back(scan);
    TrimHistoryLocked();
}

void RadioEvidence::OnCellSample(const CellSample &cell)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cell.t_ms <= 0 || cell.cell_id == 0) {
        return;
    }
    cellHistory_.push_back(cell);
    TrimHistoryLocked();

    if (stable_cell_id_ == 0) {
        last_cell_id_ = cell.cell_id;
        stable_cell_id_ = cell.cell_id;
        stable_cell_since_ms_ = cell.t_ms;
        return;
    }
    last_cell_id_ = cell.cell_id;
    if (cell.cell_id == stable_cell_id_) {
        // Still on stable cell — keep since timestamp.
        return;
    }
    // Different from stable: leave detection happens in Evaluate; do not retarget yet.
}

void RadioEvidence::OnBleSample(const BleSample &ble)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ble.t_ms <= 0 || ble.mac.empty()) {
        return;
    }
    bleHistory_.push_back(ble);
    TrimHistoryLocked();
}

std::unordered_set<std::string> RadioEvidence::BleStrongSet(const std::deque<BleSample> &hist, int64_t tMs,
    int64_t windowMs, int32_t rssiMin)
{
    std::unordered_set<std::string> out;
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
        if (tMs - it->t_ms > windowMs) {
            break;
        }
        if (it->rssi >= rssiMin && !it->mac.empty()) {
            out.insert(it->mac);
        }
    }
    return out;
}

bool RadioEvidence::RebuildSoftSetLocked(DwellState *st, int64_t tMs)
{
    if (st == nullptr) {
        return false;
    }
    std::unordered_set<std::string> prev = st->soft_set;
    std::vector<std::pair<int, std::string>> ranked;
    ranked.reserve(st->bssid_count.size());
    for (const auto &kv : st->bssid_count) {
        if (kv.second >= cfg_.dwell_min_count) {
            ranked.push_back({kv.second, kv.first});
        }
    }
    std::sort(ranked.begin(), ranked.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    st->soft_set.clear();
    for (size_t i = 0; i < ranked.size() && static_cast<int>(i) < cfg_.dwell_set_size; ++i) {
        st->soft_set.insert(ranked[i].second);
    }
    const bool readyNow = st->soft_set.size() >= 2;
    const bool changed = (st->soft_set != prev) || (readyNow != st->ready);
    st->ready = readyNow;
    if (changed && st->ready) {
        st->soft_updated_at_ms = tMs;
        st->from_persist = false;
        soft_dirty_ = true;
    }
    return changed;
}

void RadioEvidence::UpdateDwellLocked(DwellState *st, int64_t tMs, bool inside, const WifiScanSample *latestWifi)
{
    if (st == nullptr) {
        return;
    }
    if (!inside) {
        st->was_inside = false;
        st->inside_since_ms = 0;
        return;
    }
    if (!st->was_inside) {
        st->was_inside = true;
        st->inside_since_ms = tMs;
        st->last_inside_ms = tMs;
    } else if (st->last_inside_ms > 0 && tMs >= st->last_inside_ms) {
        st->inside_accum_ms += (tMs - st->last_inside_ms);
        st->last_inside_ms = tMs;
    } else {
        st->last_inside_ms = tMs;
    }

    // Already have semi-persistent soft set: still refine while INSIDE, but no learn delay.
    const bool canLearn = st->ready || st->inside_accum_ms >= cfg_.dwell_learn_ms;
    if (!canLearn) {
        return;
    }
    if (latestWifi == nullptr || latestWifi->aps.empty()) {
        return;
    }
    for (const auto &ap : latestWifi->aps) {
        if (ap.bssid.empty() || ap.rssi < cfg_.rssi_min) {
            continue;
        }
        st->bssid_count[ap.bssid] += 1;
        auto &q = st->bssid_rssi[ap.bssid];
        q.push_back(ap.rssi);
        while (q.size() > 16) {
            q.pop_front();
        }
    }
    RebuildSoftSetLocked(st, tMs);
}

void RadioEvidence::ObserveDwell(int64_t tMs, Relation relHome, Relation relCompany)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const WifiScanSample *latest = wifiHistory_.empty() ? nullptr : &wifiHistory_.back();
    UpdateDwellLocked(&home_, tMs, relHome == Relation::kInside, latest);
    UpdateDwellLocked(&company_, tMs, relCompany == Relation::kInside, latest);
}

bool RadioEvidence::DetachAgainstDwellLocked(const DwellState &st, const WifiScanSample &cur, double *jaccardOut,
    std::string *why) const
{
    if (!st.ready || st.soft_set.empty()) {
        return false;
    }
    const auto curSet = StrongSet(cur, cfg_.rssi_min);
    const double jac = Jaccard(curSet, st.soft_set);
    if (jaccardOut) {
        *jaccardOut = jac;
    }
    if (jac < cfg_.jaccard_detach) {
        if (why) {
            *why = "dwell_jaccard_low";
        }
        return true;
    }
    // RSSI drop on dwell members still visible or missing.
    int dropped = 0;
    int checked = 0;
    for (const auto &bssid : st.soft_set) {
        auto itHist = st.bssid_rssi.find(bssid);
        if (itHist == st.bssid_rssi.end() || itHist->second.empty()) {
            continue;
        }
        double med = 0.0;
        {
            std::vector<int32_t> tmp(itHist->second.begin(), itHist->second.end());
            std::sort(tmp.begin(), tmp.end());
            med = static_cast<double>(tmp[tmp.size() / 2]);
        }
        int32_t nowRssi = -127;
        bool seen = false;
        for (const auto &ap : cur.aps) {
            if (ap.bssid == bssid) {
                nowRssi = ap.rssi;
                seen = true;
                break;
            }
        }
        ++checked;
        if (!seen || (med - static_cast<double>(nowRssi)) >= cfg_.rssi_drop_db) {
            ++dropped;
        }
    }
    if (checked >= 2 && dropped * 2 >= checked) {
        if (why) {
            *why = "dwell_rssi_drop";
        }
        return true;
    }
    return false;
}

bool RadioEvidence::TemporalChurnLocked(int64_t tMs, const WifiScanSample &cur, double *jaccardOut,
    std::string *why) const
{
    if (wifiHistory_.size() < 2) {
        return false;
    }
    const auto curSet = StrongSet(cur, cfg_.rssi_min);
    if (curSet.size() < 2) {
        return false;
    }
    const WifiScanSample *base = nullptr;
    for (auto it = wifiHistory_.rbegin(); it != wifiHistory_.rend(); ++it) {
        if (tMs - it->t_ms >= cfg_.churn_baseline_ms && tMs - it->t_ms <= cfg_.churn_baseline_ms * 3) {
            base = &(*it);
            break;
        }
    }
    if (base == nullptr) {
        // Fallback: oldest in history if old enough.
        if (tMs - wifiHistory_.front().t_ms >= cfg_.churn_baseline_ms / 2) {
            base = &wifiHistory_.front();
        }
    }
    if (base == nullptr) {
        return false;
    }
    const auto baseSet = StrongSet(*base, cfg_.rssi_min);
    const double jac = Jaccard(curSet, baseSet);
    if (jaccardOut) {
        *jaccardOut = jac;
    }
    if (jac < cfg_.jaccard_churn) {
        if (why) {
            *why = "temporal_churn";
        }
        return true;
    }
    return false;
}

bool RadioEvidence::NStrongSurgeLocked(int64_t tMs, const WifiScanSample &cur) const
{
    if (wifiHistory_.size() < 2) {
        return false;
    }
    int nCur = 0;
    for (const auto &ap : cur.aps) {
        if (!ap.bssid.empty() && ap.rssi >= cfg_.rssi_strong) {
            ++nCur;
        }
    }
    const WifiScanSample *base = nullptr;
    for (auto it = wifiHistory_.rbegin(); it != wifiHistory_.rend(); ++it) {
        if (tMs - it->t_ms >= cfg_.churn_baseline_ms && tMs - it->t_ms <= cfg_.churn_baseline_ms * 3) {
            base = &(*it);
            break;
        }
    }
    if (base == nullptr) {
        return false;
    }
    int nBase = 0;
    for (const auto &ap : base->aps) {
        if (!ap.bssid.empty() && ap.rssi >= cfg_.rssi_strong) {
            ++nBase;
        }
    }
    if (nCur >= nBase + cfg_.attach_n_strong_delta) {
        return true;
    }
    if (nBase <= 2 && nCur >= cfg_.attach_n_strong_abs) {
        return true;
    }
    return false;
}

bool RadioEvidence::AttachAgainstDwellLocked(const DwellState &st, const WifiScanSample &cur, double jaccard) const
{
    if (!st.ready || st.soft_set.empty()) {
        return false;
    }
    if (jaccard < cfg_.jaccard_attach) {
        return false;
    }
    return StrongSet(cur, cfg_.rssi_min).size() >= 2;
}

bool RadioEvidence::TemporalBleChurnLocked(int64_t tMs, double *jaccardOut, std::string *why) const
{
    if (bleHistory_.size() < 4) {
        return false;
    }
    const auto cur = BleStrongSet(bleHistory_, tMs, 30000, cfg_.rssi_min);
    if (cur.size() < 2) {
        return false;
    }
    // Baseline: devices seen ~churn_baseline_ms ago (window around that age).
    std::unordered_set<std::string> base;
    for (const auto &b : bleHistory_) {
        const int64_t age = tMs - b.t_ms;
        if (age >= cfg_.churn_baseline_ms && age <= cfg_.churn_baseline_ms * 3 && b.rssi >= cfg_.rssi_min &&
            !b.mac.empty()) {
            base.insert(b.mac);
        }
    }
    if (base.size() < 2) {
        return false;
    }
    const double jac = Jaccard(cur, base);
    if (jaccardOut) {
        *jaccardOut = jac;
    }
    if (jac < cfg_.jaccard_churn) {
        if (why) {
            *why = "ble_temporal_churn";
        }
        return true;
    }
    return false;
}

bool RadioEvidence::CellLeaveLocked(int64_t tMs) const
{
    if (cellHistory_.empty() || stable_cell_id_ == 0) {
        return false;
    }
    const auto &cur = cellHistory_.back();
    if (cur.cell_id == 0 || cur.cell_id == stable_cell_id_) {
        return false;
    }
    // Prior cell must have been stable long enough before this change counts as leave.
    if (stable_cell_since_ms_ <= 0 || (tMs - stable_cell_since_ms_) < cfg_.cell_stable_ms) {
        return false;
    }
    return true;
}

RadioDetachSnapshot RadioEvidence::Evaluate(int64_t tMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RadioDetachSnapshot out;
    out.home_dwell_ready = home_.ready;
    out.company_dwell_ready = company_.ready;
    out.jaccard_home = 1.0;
    out.jaccard_company = 1.0;
    out.jaccard_churn = 1.0;

    if (wifiHistory_.empty()) {
        out.reason = "no_wifi";
        // cell-only path still possible
    }

    const WifiScanSample *curWifi = wifiHistory_.empty() ? nullptr : &wifiHistory_.back();
    std::string whyHome;
    std::string whyCo;
    std::string whyChurn;
    bool rawHome = false;
    bool rawCo = false;

    if (curWifi != nullptr) {
        if (home_.ready) {
            rawHome = DetachAgainstDwellLocked(home_, *curWifi, &out.jaccard_home, &whyHome);
        }
        if (!company_site_wifi_.empty()) {
            const auto current = StrongSet(*curWifi, cfg_.rssi_min);
            for (const auto &bssid : current) {
                if (company_site_wifi_.count(bssid) != 0) {
                    ++out.company_site_wifi_matches;
                }
            }
            out.company_site_wifi_coverage = current.empty() ? 0.0 :
                static_cast<double>(out.company_site_wifi_matches) / static_cast<double>(current.size());
            out.jaccard_company = out.company_site_wifi_coverage;
            rawCo = out.company_site_wifi_matches < 2 || out.company_site_wifi_coverage < 0.35;
        } else if (company_.ready) {
            rawCo = DetachAgainstDwellLocked(company_, *curWifi, &out.jaccard_company, &whyCo);
        }
        if (!home_.ready && !company_.ready && company_site_wifi_.empty()) {
            const bool churn = TemporalChurnLocked(tMs, *curWifi, &out.jaccard_churn, &whyChurn);
            // Without dwell sets, apply churn as generic radio leave to both sides;
            // SceneEngine still needs geo/time to pick home vs company.
            rawHome = churn;
            rawCo = churn;
        } else if (!home_.ready) {
            // Only company ready: home can still use churn as weak signal
            rawHome = TemporalChurnLocked(tMs, *curWifi, &out.jaccard_churn, &whyChurn);
        } else if (!company_.ready && company_site_wifi_.empty()) {
            rawCo = TemporalChurnLocked(tMs, *curWifi, &out.jaccard_churn, &whyChurn);
        }
    }

    auto latch = [&](bool raw, int *streak, int64_t *sinceMs) -> bool {
        if (raw) {
            if (*streak == 0) {
                *sinceMs = tMs;
            }
            ++(*streak);
            if (*streak >= cfg_.detach_confirm_scans ||
                (*sinceMs > 0 && (tMs - *sinceMs) >= cfg_.detach_confirm_ms)) {
                return true;
            }
            return false;
        }
        *streak = 0;
        *sinceMs = 0;
        return false;
    };

    out.wifi_home_detach = latch(rawHome, &home_detach_streak_, &home_detach_since_ms_);
    out.wifi_company_detach = latch(rawCo, &company_detach_streak_, &company_detach_since_ms_);
    if (!company_site_wifi_.empty() && out.wifi_company_detach) {
        company_site_wifi_detached_seen_ = true;
    }

    bool rawAttHome = false;
    bool rawAttCo = false;
    if (curWifi != nullptr) {
        out.n_strong = 0;
        for (const auto &ap : curWifi->aps) {
            if (!ap.bssid.empty() && ap.rssi >= cfg_.rssi_strong) {
                ++out.n_strong;
            }
        }
        const bool surge = company_site_wifi_.empty() && NStrongSurgeLocked(tMs, *curWifi);
        rawAttHome = AttachAgainstDwellLocked(home_, *curWifi, out.jaccard_home) || surge;
        rawAttCo = !company_site_wifi_.empty() ?
            (company_site_wifi_detached_seen_ && out.company_site_wifi_matches >= 2 &&
                out.company_site_wifi_coverage >= 0.50) :
            (AttachAgainstDwellLocked(company_, *curWifi, out.jaccard_company) || surge);
    }
    out.wifi_home_attach = latch(rawAttHome, &home_attach_streak_, &home_attach_since_ms_);
    out.wifi_company_attach = latch(rawAttCo, &company_attach_streak_, &company_attach_since_ms_);
    if (out.wifi_home_attach) {
        out.wifi_home_detach = false;
        home_detach_streak_ = 0;
        home_detach_since_ms_ = 0;
    }
    if (out.wifi_company_attach) {
        out.wifi_company_detach = false;
        company_detach_streak_ = 0;
        company_detach_since_ms_ = 0;
        company_site_wifi_detached_seen_ = false;
    }

    std::string whyBle;
    double bleJac = 1.0;
    const bool bleRaw = TemporalBleChurnLocked(tMs, &bleJac, &whyBle);
    // BLE is environment-level (no soft dwell yet): apply to both sides; focus_side gates later.
    out.ble_home_detach = latch(bleRaw, &ble_home_streak_, &ble_home_since_ms_);
    out.ble_company_detach = latch(bleRaw, &ble_company_streak_, &ble_company_since_ms_);

    const bool cellRaw = CellLeaveLocked(tMs);
    if (cellRaw) {
        ++cell_home_streak_;
        ++cell_company_streak_;
    } else {
        cell_home_streak_ = 0;
        cell_company_streak_ = 0;
    }
    out.cell_leave_home = cell_home_streak_ >= cfg_.detach_confirm_scans;
    out.cell_leave_company = cell_company_streak_ >= cfg_.detach_confirm_scans;
    if (!cellHistory_.empty() && !company_site_cells_.empty()) {
        out.company_site_cell_match = company_site_cells_.count(cellHistory_.back().cell_id) != 0;
        if (out.company_site_cell_match) {
            out.cell_leave_company = false;
        }
    }

    // Retarget stable cell: keep leave flags for this tick; quiet-adopt if prior never matured.
    if (!cellHistory_.empty()) {
        const int64_t cid = cellHistory_.back().cell_id;
        if (cid != 0 && cid != stable_cell_id_) {
            if (out.cell_leave_home || out.cell_leave_company) {
                stable_cell_id_ = cid;
                stable_cell_since_ms_ = cellHistory_.back().t_ms;
                cell_home_streak_ = 0;
                cell_company_streak_ = 0;
            } else if (stable_cell_since_ms_ <= 0 || (tMs - stable_cell_since_ms_) < cfg_.cell_stable_ms) {
                stable_cell_id_ = cid;
                stable_cell_since_ms_ = cellHistory_.back().t_ms;
                cell_home_streak_ = 0;
                cell_company_streak_ = 0;
            }
            // else: long-stable then changed — keep streaking toward leave, do not retarget yet
        } else if (cid == stable_cell_id_ && stable_cell_since_ms_ <= 0) {
            stable_cell_since_ms_ = cellHistory_.back().t_ms;
        }
    }

    // Continuous score for logging / future soft fusion.
    double s = 0.0;
    if (out.wifi_home_detach || out.wifi_company_detach) {
        s = std::max(s, 1.0);
    } else if (rawHome || rawCo) {
        s = std::max(s, 0.6);
    }
    if (out.cell_leave_home || out.cell_leave_company) {
        s = std::max(s, 0.9);
    } else if (cellRaw) {
        s = std::max(s, 0.5);
    }
    if (out.ble_home_detach || out.ble_company_detach) {
        s = std::max(s, 0.7);
    } else if (bleRaw) {
        s = std::max(s, 0.4);
    }
    if (home_.ready) {
        s = std::max(s, Clip01(1.0 - out.jaccard_home));
    } else if (!company_site_wifi_.empty()) {
        s = std::max(s, Clip01(1.0 - out.company_site_wifi_coverage));
    } else if (company_.ready) {
        s = std::max(s, Clip01(1.0 - out.jaccard_company));
    } else {
        s = std::max(s, Clip01(1.0 - out.jaccard_churn));
    }
    out.s_radio = Clip01(s);

    std::ostringstream reason;
    if (!whyHome.empty()) {
        reason << "home:" << whyHome << ";";
    }
    if (!whyCo.empty()) {
        reason << "company:" << whyCo << ";";
    }
    if (!whyChurn.empty()) {
        reason << whyChurn << ";";
    }
    if (!whyBle.empty()) {
        reason << whyBle << ";";
    }
    if (out.cell_leave_home || out.cell_leave_company) {
        reason << "cell_change;";
    }
    out.reason = reason.str();
    if (out.reason.empty()) {
        out.reason = "stable";
    }
    return out;
}

std::string RadioEvidence::DebugJson(int64_t /*tMs*/) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "{\"home_dwell_ready\":" << (home_.ready ? "true" : "false")
        << ",\"company_dwell_ready\":" << (company_.ready ? "true" : "false")
        << ",\"company_site_wifi_n\":" << company_site_wifi_.size()
        << ",\"company_site_cell_n\":" << company_site_cells_.size()
        << ",\"home_from_persist\":" << (home_.from_persist ? "true" : "false")
        << ",\"company_from_persist\":" << (company_.from_persist ? "true" : "false")
        << ",\"home_dwell_n\":" << home_.soft_set.size() << ",\"company_dwell_n\":" << company_.soft_set.size()
        << ",\"home_soft_updated_at_ms\":" << home_.soft_updated_at_ms
        << ",\"company_soft_updated_at_ms\":" << company_.soft_updated_at_ms
        << ",\"soft_dirty\":" << (soft_dirty_ ? "true" : "false")
        << ",\"wifi_history_n\":" << wifiHistory_.size() << ",\"cell_history_n\":" << cellHistory_.size()
        << ",\"home_detach_streak\":" << home_detach_streak_
        << ",\"company_detach_streak\":" << company_detach_streak_
        << ",\"cell_leave_streak\":" << cell_home_streak_
        << ",\"stable_cell_id\":" << stable_cell_id_
        << ",\"jaccard_detach_thr\":" << cfg_.jaccard_detach
        << ",\"jaccard_churn_thr\":" << cfg_.jaccard_churn << "}";
    return oss.str();
}

bool RadioEvidence::SoftDirty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return soft_dirty_;
}

void RadioEvidence::ClearSoftDirty()
{
    std::lock_guard<std::mutex> lock(mutex_);
    soft_dirty_ = false;
}

bool RadioEvidence::SoftPersistDue(int64_t nowMs) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!soft_dirty_) {
        return false;
    }
    if (last_soft_persist_ms_ <= 0) {
        return true;
    }
    return (nowMs - last_soft_persist_ms_) >= cfg_.soft_persist_min_interval_ms;
}

void RadioEvidence::MarkSoftPersisted(int64_t nowMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    soft_dirty_ = false;
    last_soft_persist_ms_ = nowMs;
}

std::string RadioEvidence::SideToJson(const DwellState &st)
{
    std::ostringstream oss;
    oss << "{\"updated_at_ms\":" << st.soft_updated_at_ms << ",\"bssids\":[";
    bool first = true;
    for (const auto &b : st.soft_set) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << b << "\"";
    }
    oss << "],\"rssi_med\":{";
    first = true;
    for (const auto &b : st.soft_set) {
        auto it = st.bssid_rssi.find(b);
        if (it == st.bssid_rssi.end() || it->second.empty()) {
            continue;
        }
        std::vector<int32_t> tmp(it->second.begin(), it->second.end());
        std::sort(tmp.begin(), tmp.end());
        const int32_t med = tmp[tmp.size() / 2];
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << b << "\":" << med;
    }
    oss << "}}";
    return oss.str();
}

std::string RadioEvidence::ExportSoftJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "{\"version\":1,\"home\":";
    if (home_.ready && home_.soft_set.size() >= 2) {
        oss << SideToJson(home_);
    } else {
        oss << "null";
    }
    oss << ",\"company\":";
    if (company_.ready && company_.soft_set.size() >= 2) {
        oss << SideToJson(company_);
    } else {
        oss << "null";
    }
    oss << "}";
    return oss.str();
}

void RadioEvidence::ApplySoftSideLocked(DwellState *st, const std::vector<std::string> &bssids,
    const std::unordered_map<std::string, int32_t> &rssiMed, int64_t updatedAtMs, int64_t nowMs)
{
    if (st == nullptr || bssids.size() < 2) {
        return;
    }
    if (updatedAtMs > 0 && cfg_.soft_persist_ttl_ms > 0 && (nowMs - updatedAtMs) > cfg_.soft_persist_ttl_ms) {
        return;
    }
    st->soft_set.clear();
    st->bssid_count.clear();
    st->bssid_rssi.clear();
    for (const auto &b : bssids) {
        if (b.empty()) {
            continue;
        }
        st->soft_set.insert(b);
        st->bssid_count[b] = cfg_.dwell_min_count;
        auto it = rssiMed.find(b);
        if (it != rssiMed.end()) {
            st->bssid_rssi[b].push_back(it->second);
        }
    }
    st->ready = st->soft_set.size() >= 2;
    st->soft_updated_at_ms = updatedAtMs > 0 ? updatedAtMs : nowMs;
    st->from_persist = st->ready;
    // Skip learn delay after restore.
    st->inside_accum_ms = cfg_.dwell_learn_ms;
}

namespace {

bool ExtractSideObject(const std::string &json, const std::string &side, std::string *obj)
{
    const std::string key = "\"" + side + "\"";
    const auto pos = json.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    const auto colon = json.find(':', pos + key.size());
    if (colon == std::string::npos) {
        return false;
    }
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) {
        ++i;
    }
    if (i < json.size() && json.compare(i, 4, "null") == 0) {
        return false;
    }
    if (i >= json.size() || json[i] != '{') {
        return false;
    }
    int depth = 0;
    for (size_t j = i; j < json.size(); ++j) {
        if (json[j] == '{') {
            ++depth;
        } else if (json[j] == '}') {
            --depth;
            if (depth == 0) {
                *obj = json.substr(i, j - i + 1);
                return true;
            }
        }
    }
    return false;
}

int64_t ExtractUpdatedAt(const std::string &obj)
{
    const std::string key = "\"updated_at_ms\"";
    const auto pos = obj.find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    const auto colon = obj.find(':', pos + key.size());
    if (colon == std::string::npos) {
        return 0;
    }
    size_t i = colon + 1;
    while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) {
        ++i;
    }
    try {
        return std::stoll(obj.substr(i));
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> ExtractBssids(const std::string &obj)
{
    std::vector<std::string> out;
    const std::string key = "\"bssids\"";
    const auto pos = obj.find(key);
    if (pos == std::string::npos) {
        return out;
    }
    const auto lb = obj.find('[', pos + key.size());
    const auto rb = obj.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) {
        return out;
    }
    const std::string arr = obj.substr(lb + 1, rb - lb - 1);
    size_t i = 0;
    while (i < arr.size()) {
        const auto q1 = arr.find('"', i);
        if (q1 == std::string::npos) {
            break;
        }
        const auto q2 = arr.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            break;
        }
        out.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

std::unordered_map<std::string, int32_t> ExtractRssiMed(const std::string &obj)
{
    std::unordered_map<std::string, int32_t> out;
    const std::string key = "\"rssi_med\"";
    const auto pos = obj.find(key);
    if (pos == std::string::npos) {
        return out;
    }
    const auto lb = obj.find('{', pos + key.size());
    if (lb == std::string::npos) {
        return out;
    }
    int depth = 0;
    size_t rb = std::string::npos;
    for (size_t j = lb; j < obj.size(); ++j) {
        if (obj[j] == '{') {
            ++depth;
        } else if (obj[j] == '}') {
            --depth;
            if (depth == 0) {
                rb = j;
                break;
            }
        }
    }
    if (rb == std::string::npos) {
        return out;
    }
    const std::string body = obj.substr(lb + 1, rb - lb - 1);
    size_t i = 0;
    while (i < body.size()) {
        const auto q1 = body.find('"', i);
        if (q1 == std::string::npos) {
            break;
        }
        const auto q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            break;
        }
        const std::string bssid = body.substr(q1 + 1, q2 - q1 - 1);
        const auto colon = body.find(':', q2 + 1);
        if (colon == std::string::npos) {
            break;
        }
        size_t n = colon + 1;
        while (n < body.size() && (body[n] == ' ' || body[n] == '\t')) {
            ++n;
        }
        try {
            out[bssid] = static_cast<int32_t>(std::stoi(body.substr(n)));
        } catch (...) {
        }
        i = n;
        const auto comma = body.find(',', n);
        if (comma == std::string::npos) {
            break;
        }
        i = comma + 1;
    }
    return out;
}

}  // namespace

bool RadioEvidence::ImportSoftJson(const std::string &json, int64_t nowMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool any = false;
    std::string homeObj;
    if (ExtractSideObject(json, "home", &homeObj)) {
        ApplySoftSideLocked(&home_, ExtractBssids(homeObj), ExtractRssiMed(homeObj), ExtractUpdatedAt(homeObj),
            nowMs);
        any = any || home_.ready;
    }
    std::string coObj;
    if (ExtractSideObject(json, "company", &coObj)) {
        ApplySoftSideLocked(&company_, ExtractBssids(coObj), ExtractRssiMed(coObj), ExtractUpdatedAt(coObj), nowMs);
        any = any || company_.ready;
    }
    soft_dirty_ = false;
    return any;
}

bool RadioEvidence::ImportCompanySiteJson(const std::string &json)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string companyObj;
    if (!ExtractSideObject(json, "company", &companyObj)) {
        companyObj = json;
    }
    std::string wifiObj;
    std::string cellObj;
    const bool hasWifi = ExtractSideObject(companyObj, "wifi", &wifiObj);
    const bool hasCell = ExtractSideObject(companyObj, "cell", &cellObj);
    company_site_wifi_.clear();
    company_site_cells_.clear();
    if (hasWifi) {
        for (const auto &bssid : ExtractBssids(wifiObj)) {
            if (!bssid.empty()) {
                company_site_wifi_.insert(Lower(bssid));
            }
        }
    }
    if (hasCell) {
        const std::string key = "\"cell_ids\"";
        const auto pos = cellObj.find(key);
        const auto lb = pos == std::string::npos ? std::string::npos : cellObj.find('[', pos + key.size());
        const auto rb = lb == std::string::npos ? std::string::npos : cellObj.find(']', lb);
        if (lb != std::string::npos && rb != std::string::npos) {
            std::stringstream values(cellObj.substr(lb + 1, rb - lb - 1));
            std::string value;
            while (std::getline(values, value, ',')) {
                try {
                    const int64_t cellId = std::stoll(value);
                    if (cellId != 0) {
                        company_site_cells_.insert(cellId);
                    }
                } catch (...) {
                }
            }
        }
    }
    company_site_wifi_detached_seen_ = false;
    return !company_site_wifi_.empty() || !company_site_cells_.empty();
}

}  // namespace commute_sa
