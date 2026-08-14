#include "commute_sa/baseline_runtime.h"

#include "commute_sa/product_store.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace commute_sa {
namespace {

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void MaybePersistRadioSoft(RadioEvidence *radio, int64_t tMs)
{
    if (radio == nullptr || !radio->SoftPersistDue(tMs)) {
        return;
    }
    const std::string json = radio->ExportSoftJson();
    if (ProductStore::GetInstance().SaveRadioSoftJson(json)) {
        radio->MarkSoftPersisted(tMs);
    }
}

bool ReadTextFile(const std::string &path, std::string *out)
{
    if (out == nullptr) {
        return false;
    }
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::ostringstream body;
    body << in.rdbuf();
    *out = body.str();
    return !out->empty();
}

}  // namespace

BaselineRuntime &BaselineRuntime::GetInstance()
{
    static BaselineRuntime inst;
    return inst;
}

void BaselineRuntime::Init(const std::string &anchorsPath, const std::string &thetaPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    anchorsPath_ = anchorsPath;
    thetaPath_ = thetaPath;
    AnchorSet anchors = DefaultAnchors();
    Theta theta = DefaultTheta();
    if (!anchorsPath.empty()) {
        LoadAnchorsFromFile(anchorsPath, &anchors, nullptr);
    }
    if (!thetaPath.empty()) {
        LoadThetaFromFile(thetaPath, &theta, nullptr);
    }
    delete engine_;
    engine_ = new SceneEngine(anchors, theta);
    PersonalizationPolicy policy = DefaultPersonalizationPolicy();
    const std::string root = ProductStore::GetInstance().RootDir();
    if (!root.empty()) {
        LoadPersonalizationPolicyFromFile(root + "/policy.json", &policy, nullptr);
    }
    engine_->SetPersonalizationPolicy(policy);
    radio_.Reset(false);
    pdr_.Reset();
    baro_.Reset();
    std::string softJson;
    if (ProductStore::GetInstance().LoadRadioSoftJson(&softJson)) {
        radio_.ImportSoftJson(softJson, NowMs());
    }
    std::string siteJson;
    if (!root.empty() && ReadTextFile(root + "/company_radio_fingerprint.json", &siteJson)) {
        radio_.ImportCompanySiteJson(siteJson);
    }
    inited_ = true;
}

bool BaselineRuntime::Enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_ && inited_ && engine_ != nullptr;
}

bool BaselineRuntime::Walking() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return walking_;
}

SceneEngine *BaselineRuntime::Engine()
{
    return engine_;
}

RadioEvidence *BaselineRuntime::Radio()
{
    return &radio_;
}

PdrEvidence *BaselineRuntime::Pdr()
{
    return &pdr_;
}

void BaselineRuntime::OnWalkingStarted(int64_t tsMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = true;
    hasWalkStarted_ = true;
    walkStartedAtMs_ = tsMs;
    pdr_.OnWalkingStarted(tsMs);
}

void BaselineRuntime::OnWalkingStopped(int64_t tsMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = false;
    hasWalkStarted_ = false;
    pdr_.OnWalkingStopped(tsMs);
}

void BaselineRuntime::OnWifiScan(const WifiScanSample &scan)
{
    // RadioEvidence has its own mutex; avoid taking BaselineRuntime lock here
    // so WiFi callbacks do not stall behind OnTick.
    radio_.OnWifiScan(scan);
}

void BaselineRuntime::OnCellSample(const CellSample &cell)
{
    radio_.OnCellSample(cell);
}

void BaselineRuntime::OnBleSample(const BleSample &ble)
{
    radio_.OnBleSample(ble);
}

void BaselineRuntime::OnPdrPoint(int64_t tMs, double xM, double yM)
{
    // Own mutex inside PdrEvidence; avoid holding BaselineRuntime lock so PDR
    // callbacks do not stall behind OnTick.
    pdr_.OnPdrPoint(tMs, xM, yM);
}

void BaselineRuntime::OnBaro(int64_t tMs, double pressureHpa)
{
    std::lock_guard<std::mutex> lock(mutex_);
    baro_.Observe(tMs, pressureHpa);
}

TickDecision BaselineRuntime::OnTick(
    int64_t tMs, bool hasGps, double lat, double lon, double accM, bool gpsValid, int32_t gpsSourceType)
{
    std::lock_guard<std::mutex> lock(mutex_);
    TickDecision empty;
    if (!inited_ || engine_ == nullptr || !enabled_) {
        return empty;
    }
    const RadioDetachSnapshot radioSnap = radio_.Evaluate(tMs);

    TickFeatures feat;
    feat.t_ms = tMs;
    feat.has_gps = hasGps && gpsValid;
    feat.gps_source_type = gpsSourceType;
    feat.lat = lat;
    feat.lon = lon;
    feat.acc = accM;
    feat.gps_source_type = gpsSourceType;
    feat.walking = walking_;
    feat.has_walk_started = hasWalkStarted_;
    feat.walk_started_at_ms = walkStartedAtMs_;
    feat.wifi_home_detach = radioSnap.wifi_home_detach;
    feat.wifi_company_detach = radioSnap.wifi_company_detach;
    feat.wifi_home_attach = radioSnap.wifi_home_attach;
    feat.wifi_company_attach = radioSnap.wifi_company_attach;
    feat.cell_leave_home = radioSnap.cell_leave_home;
    feat.cell_leave_company = radioSnap.cell_leave_company;
    feat.ble_home_detach = radioSnap.ble_home_detach;
    feat.ble_company_detach = radioSnap.ble_company_detach;
    feat.wifi_jaccard_home = radioSnap.jaccard_home;
    feat.wifi_jaccard_company = radioSnap.jaccard_company;
    const bool workplaceReady = radioSnap.company_dwell_ready || radioSnap.company_site_wifi_coverage >= 0.50 ||
        radioSnap.company_site_cell_match;
    const BaroSnapshot baroSnap = baro_.Evaluate(tMs, workplaceReady,
        engine_->GetTheta().baro_min_descent_m);
    feat.baro_available = baroSnap.available;
    feat.baro_baseline_ready = baroSnap.baseline_ready;
    feat.baro_descent_m = baroSnap.descent_m;
    feat.baro_descending = baroSnap.descending;
    feat.baro_stable_platform = baroSnap.stable_platform;
    feat.baro_lower_platform = baroSnap.lower_platform;
    if (engine_ != nullptr) {
        if (!FocusAllowsHome(engine_->GetTheta().focus_side)) {
            feat.wifi_home_detach = false;
            feat.wifi_home_attach = false;
            feat.cell_leave_home = false;
            feat.ble_home_detach = false;
            feat.wifi_jaccard_home = 1.0;
        }
        if (!FocusAllowsCompany(engine_->GetTheta().focus_side)) {
            feat.wifi_company_detach = false;
            feat.wifi_company_attach = false;
            feat.cell_leave_company = false;
            feat.ble_company_detach = false;
            feat.wifi_jaccard_company = 1.0;
        }
    }

    // Pre-tag walk side from current GPS before scoring (first INSIDE/NEAR sticks).
    if (feat.has_gps && walking_) {
        const auto &anchors = engine_->GetAnchors();
        const Relation hPre =
            RelationToAnchor(feat.lat, feat.lon, anchors.home.lat, anchors.home.lon, anchors.home.r_in_m,
                anchors.home.r_out_m, nullptr);
        const Relation cPre =
            RelationToAnchor(feat.lat, feat.lon, anchors.company.lat, anchors.company.lon, anchors.company.r_in_m,
                anchors.company.r_out_m, nullptr);
        pdr_.NoteWalkContext(hPre, cPre);
    }
    const PdrLeaveSnapshot pdrSnap = pdr_.Evaluate();
    feat.pdr_net_out_home_m = pdrSnap.pdr_net_out_home_m;
    feat.pdr_net_out_company_m = pdrSnap.pdr_net_out_company_m;
    if (engine_ != nullptr) {
        if (!FocusAllowsHome(engine_->GetTheta().focus_side)) {
            feat.pdr_net_out_home_m = 0.0;
        }
        if (!FocusAllowsCompany(engine_->GetTheta().focus_side)) {
            feat.pdr_net_out_company_m = 0.0;
        }
    }

    TickDecision dec = engine_->Step(feat);
    ProductStore::GetInstance().ObservePolicyFeatures(feat, dec);
    if (FocusAllowsHome(engine_->GetTheta().focus_side) || FocusAllowsCompany(engine_->GetTheta().focus_side)) {
        const Relation dwellHome =
            FocusAllowsHome(engine_->GetTheta().focus_side) ? dec.home_relation : Relation::kOutside;
        const Relation dwellCo =
            FocusAllowsCompany(engine_->GetTheta().focus_side) ? dec.company_relation : Relation::kOutside;
        radio_.ObserveDwell(tMs, dwellHome, dwellCo);
    }
    MaybePersistRadioSoft(&radio_, tMs);
    return dec;
}

bool BaselineRuntime::PersistTheta() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ == nullptr) {
        return false;
    }
    return ProductStore::GetInstance().SaveTheta(engine_->GetTheta());
}

bool BaselineRuntime::ApplyThetaDeltaAndPersist(const std::string &param, double delta, const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ == nullptr) {
        return false;
    }
    Theta t = engine_->GetTheta();
    auto readV = [&](const Theta &th, double *out) -> bool {
        if (param == "enter_leave") {
            *out = th.enter_leave;
            return true;
        }
        if (param == "exit_leave") {
            *out = th.exit_leave;
            return true;
        }
        if (param == "w_walk") {
            *out = th.w_walk;
            return true;
        }
        if (param == "w_pdr") {
            *out = th.w_pdr;
            return true;
        }
        if (param == "w_geo") {
            *out = th.w_geo;
            return true;
        }
        if (param == "w_wifi") {
            *out = th.w_wifi;
            return true;
        }
        if (param == "w_cell") {
            *out = th.w_cell;
            return true;
        }
        if (param == "w_ble") {
            *out = th.w_ble;
            return true;
        }
        if (param == "w_radio") {
            *out = th.w_wifi + th.w_cell + th.w_ble;
            return true;
        }
        if (param == "w_time") {
            *out = th.w_time;
            return true;
        }
        if (param == "w_baro") {
            *out = th.w_baro;
            return true;
        }
        if (param == "weekday_leave_home_hour") {
            *out = th.weekday_leave_home_hour;
            return true;
        }
        if (param == "weekday_leave_company_hour") {
            *out = th.weekday_leave_company_hour;
            return true;
        }
        if (param == "arm_delay_s") {
            *out = th.arm_delay_s;
            return true;
        }
        if (param == "lead_min_s") {
            *out = th.lead_min_s;
            return true;
        }
        if (param == "lead_max_s") {
            *out = th.lead_max_s;
            return true;
        }
        return false;
    };
    double oldV = 0.0;
    if (!readV(t, &oldV)) {
        return false;
    }
    if (!ApplyThetaDelta(&t, param, delta, nullptr)) {
        return false;
    }
    double newV = oldV;
    readV(t, &newV);
    engine_->SetTheta(t);
    ProductStore::GetInstance().AppendParamChange(NowMs(), param, oldV, newV, reason);
    return ProductStore::GetInstance().SaveTheta(t);
}

bool BaselineRuntime::ApplyPersonalizationPolicyAndPersist(const PersonalizationPolicy &policy)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ == nullptr || !ValidatePersonalizationPolicy(policy, nullptr)) {
        return false;
    }
    const std::string root = ProductStore::GetInstance().RootDir();
    if (root.empty() || !SavePersonalizationPolicyToFile(root + "/policy.json", policy, nullptr)) {
        return false;
    }
    engine_->SetPersonalizationPolicy(policy);
    return true;
}

std::string BaselineRuntime::RadioDebugJson(int64_t tMs) const
{
    return radio_.DebugJson(tMs);
}

std::string BaselineRuntime::PdrDebugJson() const
{
    return pdr_.DebugJson();
}

}  // namespace commute_sa
