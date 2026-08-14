#include "commute_sa/product_store.h"

#include "commute_sa/anchors.h"
#include "commute_sa/theta.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define COMMUTE_SA_MKDIR(path) _mkdir(path)
#else
#define COMMUTE_SA_MKDIR(path) mkdir(path, 0755)
#endif

namespace commute_sa {
namespace {

bool EnsureDir(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        current.push_back(c);
        if ((c == '/' || c == '\\') && current.size() > 1) {
            if (current.size() == 3 && current[1] == ':') {
                continue;
            }
            if (stat(current.c_str(), &st) != 0) {
                if (COMMUTE_SA_MKDIR(current.c_str()) != 0) {
                    if (stat(current.c_str(), &st) != 0) {
                        return false;
                    }
                }
            }
        }
    }
    if (stat(path.c_str(), &st) != 0) {
        return COMMUTE_SA_MKDIR(path.c_str()) == 0 || stat(path.c_str(), &st) == 0;
    }
    return true;
}

std::string Esc(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        if (c == '\n' || c == '\r') {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

ProductStore &ProductStore::GetInstance()
{
    static ProductStore inst;
    return inst;
}

bool ProductStore::Init(const std::string &rootDir)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_ && root_ == rootDir) {
        return true;
    }
    leaveEpisodes_.close();
    paramChanges_.close();
    personalizeJobs_.close();
    sparseSamples_.close();
    auditLog_.close();
    anchorJobs_.close();
    policyHistory_.close();
    root_ = rootDir;
    if (!EnsureDir(root_)) {
        inited_ = false;
        return false;
    }
    leaveEpisodes_.open(root_ + "/leave_episodes.jsonl", std::ios::out | std::ios::app);
    paramChanges_.open(root_ + "/param_changes.jsonl", std::ios::out | std::ios::app);
    personalizeJobs_.open(root_ + "/personalize_jobs.jsonl", std::ios::out | std::ios::app);
    sparseSamples_.open(root_ + "/leave_window_samples.jsonl", std::ios::out | std::ios::app);
    auditLog_.open(root_ + "/audit.jsonl", std::ios::out | std::ios::app);
    anchorJobs_.open(root_ + "/anchor_reestimate_jobs.jsonl", std::ios::out | std::ios::app);
    policyHistory_.open(root_ + "/policy_history.jsonl", std::ios::out | std::ios::app);
    inited_ = leaveEpisodes_.is_open() && paramChanges_.is_open() && personalizeJobs_.is_open() &&
        auditLog_.is_open() && anchorJobs_.is_open() && policyHistory_.is_open();
    return inited_;
}

const std::string &ProductStore::RootDir() const
{
    return root_;
}

void ProductStore::AppendLine(std::ofstream &f, const std::string &line)
{
    if (!f.is_open()) {
        return;
    }
    f << line << "\n";
    f.flush();
}

void ProductStore::AppendLeavePush(int64_t tPushMs, const std::string &intent, const std::string &scene,
    double scoreHome, double scoreCompany, bool hasDistHome, double distHomeM, bool walking, double enterLeave,
    int minEvidence, const std::string &homeRelation, double etaLeaveS)
{
    std::lock_guard<std::mutex> lock(mutex_);
    activePushMs_ = tPushMs;
    activeIntent_ = intent;
    tStarOutsideMs_ = 0;
    if (intent == "LEAVE_COMPANY_NOTIFICATION") {
        lastCompanyPushMs_ = tPushMs;
    } else {
        lastHomePushMs_ = tPushMs;
    }
    std::ostringstream oss;
    oss << std::setprecision(8)
        << "{\"type\":\"push\",\"t_push_ms\":" << tPushMs << ",\"intent\":\"" << Esc(intent) << "\",\"scene\":\""
        << Esc(scene) << "\",\"score_home\":" << scoreHome << ",\"score_company\":" << scoreCompany
        << ",\"dist_home_m\":" << (hasDistHome ? distHomeM : -1.0) << ",\"walking\":" << (walking ? "true" : "false")
        << ",\"home_relation\":\"" << Esc(homeRelation) << "\",\"eta_leave_s\":" << etaLeaveS
        << ",\"theta\":{\"enter_leave\":" << enterLeave << ",\"min_evidence\":" << minEvidence << "}}";
    AppendLine(leaveEpisodes_, oss.str());
}

bool ProductStore::ObserveLeaveProgress(int64_t tMs, const std::string &homeRelation,
    const std::string &companyRelation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (activePushMs_ <= 0 || tStarOutsideMs_ > 0) {
        return false;
    }
    const bool leaveCompany = (activeIntent_ == "LEAVE_COMPANY_NOTIFICATION");
    const std::string &rel = leaveCompany ? companyRelation : homeRelation;
    if (rel == "OUTSIDE") {
        tStarOutsideMs_ = tMs;
        return true;
    }
    return false;
}

bool ProductStore::ObserveMissedLeave(int64_t tMs, const std::string &homeRelation,
    const std::string &companyRelation, double awayConfirmS, double lookbackS, const std::string &focusSide,
    std::string *sideOut)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_) {
        return false;
    }
    const int64_t confirmMs = static_cast<int64_t>(std::max(30.0, awayConfirmS) * 1000.0);
    const int64_t lookbackMs = static_cast<int64_t>(std::max(60.0, lookbackS) * 1000.0);

    auto tickSide = [&](const std::string &rel, int64_t *outsideSince, bool *emitted, bool *seenInside,
                         int64_t lastPushMs, const std::string &side, const std::string &pushIntent) -> bool {
        if (rel == "INSIDE" || rel == "NEAR") {
            *seenInside = true;
            *outsideSince = 0;
            *emitted = false;
            return false;
        }
        if (rel != "OUTSIDE") {
            return false;
        }
        // Never mark missed if we never observed INSIDE/NEAR (boot already outside / commute).
        if (!*seenInside) {
            return false;
        }
        if (*outsideSince <= 0) {
            *outsideSince = tMs;
            return false;
        }
        if (*emitted) {
            return false;
        }
        if ((tMs - *outsideSince) < confirmMs) {
            return false;
        }
        // Covered by an active push for this side → CONFIRMED path, not missed.
        if (activePushMs_ > 0 && activeIntent_ == pushIntent && activePushMs_ <= *outsideSince) {
            *emitted = true;
            return false;
        }
        // Recent push before leaving → not a miss.
        if (lastPushMs > 0 && lastPushMs >= (*outsideSince - lookbackMs) && lastPushMs <= *outsideSince) {
            *emitted = true;
            return false;
        }
        *emitted = true;
        std::ostringstream oss;
        oss << "{\"type\":\"label\",\"t_label_ms\":" << tMs << ",\"t_push_ms\":0"
            << ",\"label\":\"MISSED_LEAVE\",\"side\":\"" << Esc(side) << "\",\"home_relation\":\""
            << Esc(homeRelation) << "\",\"company_relation\":\"" << Esc(companyRelation)
            << "\",\"t_star_ms\":" << *outsideSince << ",\"lead_s\":null,\"away_confirm_s\":" << awayConfirmS
            << ",\"lookback_s\":" << lookbackS << "}";
        AppendLine(leaveEpisodes_, oss.str());
        FlushPolicyHistoryLocked(side, "MISSED_LEAVE", *outsideSince);
        if (sideOut) {
            *sideOut = side;
        }
        return true;
    };

    bool fired = false;
    if (FocusAllowsHome(focusSide)) {
        fired = tickSide(homeRelation, &outsideHomeSinceMs_, &missedHomeEmitted_, &seenInsideHome_, lastHomePushMs_,
                    "home", "DEPARTURE_NOTIFICATION") ||
            fired;
    }
    if (FocusAllowsCompany(focusSide)) {
        fired = tickSide(companyRelation, &outsideCompanySinceMs_, &missedCompanyEmitted_, &seenInsideCompany_,
                    lastCompanyPushMs_, "company", "LEAVE_COMPANY_NOTIFICATION") ||
            fired;
    }
    return fired;
}

void ProductStore::AppendLeaveLabel(int64_t tLabelMs, int64_t tPushMs, const std::string &label,
    const std::string &homeRelation, bool hasDistHome, double distHomeM)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string side = activeIntent_ == "LEAVE_COMPANY_NOTIFICATION" ? "company" : "home";
    const int64_t outcomeMs = (label == "CONFIRMED_LEAVE" && tStarOutsideMs_ > 0) ? tStarOutsideMs_ :
        ((label == "FALSE_PUSH" && tPushMs > 0) ? tPushMs : tLabelMs);
    FlushPolicyHistoryLocked(side, label, outcomeMs);
    std::ostringstream oss;
    oss << std::setprecision(8) << "{\"type\":\"label\",\"t_label_ms\":" << tLabelMs << ",\"t_push_ms\":" << tPushMs
        << ",\"label\":\"" << Esc(label) << "\",\"home_relation\":\"" << Esc(homeRelation)
        << "\",\"dist_home_m\":" << (hasDistHome ? distHomeM : -1.0);
    if (tStarOutsideMs_ > 0 && tPushMs > 0 && tStarOutsideMs_ >= tPushMs) {
        const double leadS = static_cast<double>(tStarOutsideMs_ - tPushMs) / 1000.0;
        oss << ",\"t_star_ms\":" << tStarOutsideMs_ << ",\"lead_s\":" << leadS;
    } else {
        oss << ",\"t_star_ms\":null,\"lead_s\":null";
    }
    oss << "}";
    AppendLine(leaveEpisodes_, oss.str());
    if (tPushMs == activePushMs_) {
        activePushMs_ = 0;
        activeIntent_.clear();
        tStarOutsideMs_ = 0;
    }
}

void ProductStore::ObservePolicyFeatures(const TickFeatures &f, const TickDecision &d)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_ || !policyHistory_.is_open() || (lastPolicySampleMs_ > 0 && f.t_ms - lastPolicySampleMs_ < 5000)) {
        return;
    }
    const bool radioHome = f.wifi_home_detach || f.cell_leave_home || f.ble_home_detach;
    const bool radioCompany = f.wifi_company_detach || f.cell_leave_company || f.ble_company_detach;
    if (!f.walking && !radioHome && !radioCompany && d.hsmm_preleave_home < 0.05 && d.hsmm_preleave_company < 0.05) {
        return;
    }
    lastPolicySampleMs_ = f.t_ms;
    auto add = [&](const std::string &side, double preleave, double leaving, int hits, double pdr,
                   bool wifi, bool cell, bool ble, Relation rel, const LeaveObservation &obs) {
        PolicyHistoryRow row;
        row.t_ms = f.t_ms;
        row.side = side;
        row.preleave_probability = preleave;
        row.leaving_probability = leaving;
        row.hits = hits;
        row.walking = f.walking;
        row.pdr_net_out_m = pdr;
        row.wifi_detach = wifi;
        row.cell_leave = cell;
        row.ble_detach = ble;
        row.has_usable_gps = f.has_gps && rel != Relation::kUnknown;
        row.baro_available = f.baro_available;
        row.baro_baseline_ready = f.baro_baseline_ready;
        row.baro_descent_m = f.baro_descent_m;
        row.baro_lower_platform = f.baro_lower_platform;
        row.hsmm_obs = obs;
        policyBuffer_.push_back(row);
    };
    add("home", d.hsmm_preleave_home, d.score_home, d.hits_home, f.pdr_net_out_home_m,
        f.wifi_home_detach, f.cell_leave_home, f.ble_home_detach, d.home_relation, d.hsmm_obs_home);
    add("company", d.hsmm_preleave_company, d.score_company, d.hits_company, f.pdr_net_out_company_m,
        f.wifi_company_detach, f.cell_leave_company, f.ble_company_detach, d.company_relation, d.hsmm_obs_company);
    const int64_t keepAfter = f.t_ms - 30 * 60 * 1000;
    while (!policyBuffer_.empty() && policyBuffer_.front().t_ms < keepAfter) policyBuffer_.pop_front();
}

void ProductStore::FlushPolicyHistoryLocked(const std::string &side, const std::string &label, int64_t outcomeMs)
{
    const int64_t begin = outcomeMs - 10 * 60 * 1000;
    for (const auto &row : policyBuffer_) {
        if (row.side != side || row.t_ms < begin || row.t_ms > outcomeMs) continue;
        std::ostringstream out;
        out << std::setprecision(8) << "{\"t_ms\":" << row.t_ms << ",\"outcome_t_ms\":" << outcomeMs
            << ",\"side\":\"" << Esc(side) << "\",\"label\":\"" << Esc(label)
            << "\",\"preleave_probability\":" << row.preleave_probability
            << ",\"leaving_probability\":" << row.leaving_probability << ",\"hits\":" << row.hits
            << ",\"walking\":" << (row.walking ? "true" : "false")
            << ",\"wifi_detach\":" << (row.wifi_detach ? "true" : "false")
            << ",\"cell_leave\":" << (row.cell_leave ? "true" : "false")
            << ",\"ble_detach\":" << (row.ble_detach ? "true" : "false")
            << ",\"pdr_net_out_m\":" << row.pdr_net_out_m
            << ",\"geo_outbound\":false,\"has_usable_gps\":" << (row.has_usable_gps ? "true" : "false")
            << ",\"baro_available\":" << (row.baro_available ? "true" : "false")
            << ",\"baro_baseline_ready\":" << (row.baro_baseline_ready ? "true" : "false")
            << ",\"baro_descent_m\":" << row.baro_descent_m
            << ",\"baro_lower_platform\":" << (row.baro_lower_platform ? "true" : "false")
            << ",\"obs_walking\":" << row.hsmm_obs.walking
            << ",\"obs_pdr_outbound\":" << row.hsmm_obs.pdr_outbound
            << ",\"obs_geo_outbound\":" << row.hsmm_obs.geo_outbound
            << ",\"obs_wifi_detach\":" << row.hsmm_obs.wifi_detach
            << ",\"obs_cell_detach\":" << row.hsmm_obs.cell_detach
            << ",\"obs_ble_detach\":" << row.hsmm_obs.ble_detach
            << ",\"obs_time_prior\":" << row.hsmm_obs.time_prior
            << ",\"obs_baro_descending\":" << row.hsmm_obs.baro_descending
            << ",\"obs_baro_lower_platform\":" << row.hsmm_obs.baro_lower_platform
            << ",\"obs_baro_available\":" << (row.hsmm_obs.baro_available ? "true" : "false")
            << ",\"obs_relation_known\":" << (row.hsmm_obs.relation_known ? "true" : "false")
            << ",\"obs_inside\":" << (row.hsmm_obs.inside ? "true" : "false")
            << ",\"obs_near\":" << (row.hsmm_obs.near ? "true" : "false")
            << ",\"obs_outside\":" << (row.hsmm_obs.outside ? "true" : "false")
            << ",\"obs_approaching\":" << (row.hsmm_obs.approaching ? "true" : "false")
            << ",\"obs_attached\":" << (row.hsmm_obs.attached ? "true" : "false")
            << ",\"lead_s\":" << static_cast<double>(outcomeMs - row.t_ms) / 1000.0 << "}";
        AppendLine(policyHistory_, out.str());
    }
    policyBuffer_.erase(std::remove_if(policyBuffer_.begin(), policyBuffer_.end(), [&](const PolicyHistoryRow &row) {
        return row.side == side && row.t_ms <= outcomeMs;
    }), policyBuffer_.end());
}

void ProductStore::AppendSparseSample(int64_t tMs, int64_t tPushMs, double lat, double lon, double acc, bool walking,
    const std::string &homeRelation, double distHomeM)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (tPushMs <= 0 && activePushMs_ <= 0) {
        return;
    }
    const int64_t push = tPushMs > 0 ? tPushMs : activePushMs_;
    std::ostringstream oss;
    oss << std::setprecision(8) << "{\"t_ms\":" << tMs << ",\"t_push_ms\":" << push << ",\"lat\":" << lat
        << ",\"lon\":" << lon << ",\"acc\":" << acc << ",\"walking\":" << (walking ? "true" : "false")
        << ",\"home_relation\":\"" << Esc(homeRelation) << "\",\"dist_home_m\":" << distHomeM << "}";
    AppendLine(sparseSamples_, oss.str());
}

void ProductStore::AppendPersonalizeJob(int64_t createdAtMs, const std::string &reason, const std::string &lastIntent,
    const std::string &lastScene, int64_t lastPushAtMs, const Theta &theta)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "{\"created_at_ms\":" << createdAtMs << ",\"reason\":\"" << Esc(reason) << "\",\"last_intent\":\""
        << Esc(lastIntent) << "\",\"last_scene\":\"" << Esc(lastScene) << "\",\"last_push_at_ms\":" << lastPushAtMs
        << ",\"enter_leave\":" << theta.enter_leave << ",\"w_walk\":" << theta.w_walk
        << ",\"weekday_leave_home_hour\":" << theta.weekday_leave_home_hour << "}";
    AppendLine(personalizeJobs_, oss.str());
}

void ProductStore::AppendParamChange(
    int64_t tMs, const std::string &param, double oldValue, double newValue, const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "{\"t_ms\":" << tMs << ",\"param\":\"" << Esc(param) << "\",\"old\":" << oldValue << ",\"new\":" << newValue
        << ",\"reason\":\"" << Esc(reason) << "\"}";
    AppendLine(paramChanges_, oss.str());
    RecentParamChange rec;
    rec.t_ms = tMs;
    rec.param = param;
    rec.old_value = oldValue;
    rec.new_value = newValue;
    rec.reason = reason;
    recentParamChanges_.push_back(rec);
    while (recentParamChanges_.size() > kRecentChangeCap) {
        recentParamChanges_.pop_front();
    }
}

std::string ProductStore::AppendAudit(int64_t tMs, const std::string &message, const std::string &changesJson)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_ || !auditLog_.is_open()) {
        return "";
    }
    ++auditSeq_;
    std::ostringstream id;
    id << "audit-" << tMs << "-" << auditSeq_;
    const std::string changes = changesJson.empty() ? "{}" : changesJson;
    std::ostringstream oss;
    oss << "{\"audit_id\":\"" << Esc(id.str()) << "\",\"t_ms\":" << tMs << ",\"message\":\"" << Esc(message)
        << "\",\"changes\":" << changes << "}";
    AppendLine(auditLog_, oss.str());
    RecentAudit rec;
    rec.t_ms = tMs;
    rec.audit_id = id.str();
    rec.message = message;
    rec.changes_json = changes;
    recentAudits_.push_back(rec);
    while (recentAudits_.size() > kRecentAuditCap) {
        recentAudits_.pop_front();
    }
    return id.str();
}

std::string ProductStore::GetRecentParamChangesJson(int64_t sinceMs, int limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    int n = 0;
    for (auto it = recentParamChanges_.rbegin(); it != recentParamChanges_.rend(); ++it) {
        if (sinceMs > 0 && it->t_ms < sinceMs) {
            continue;
        }
        if (n >= limit) {
            break;
        }
        if (!first) {
            oss << ",";
        }
        first = false;
        ++n;
        oss << "{\"t_ms\":" << it->t_ms << ",\"param\":\"" << Esc(it->param) << "\",\"old\":" << it->old_value
            << ",\"new\":" << it->new_value << ",\"reason\":\"" << Esc(it->reason) << "\"}";
    }
    oss << "]";
    return oss.str();
}

std::string ProductStore::GetRecentAuditsJson(int64_t sinceMs, int limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    int n = 0;
    for (auto it = recentAudits_.rbegin(); it != recentAudits_.rend(); ++it) {
        if (sinceMs > 0 && it->t_ms < sinceMs) {
            continue;
        }
        if (n >= limit) {
            break;
        }
        if (!first) {
            oss << ",";
        }
        first = false;
        ++n;
        oss << "{\"t_ms\":" << it->t_ms << ",\"audit_id\":\"" << Esc(it->audit_id) << "\",\"message\":\""
            << Esc(it->message) << "\",\"changes\":" << it->changes_json << "}";
    }
    oss << "]";
    return oss.str();
}

std::string ProductStore::AppendAnchorReestimateJob(int64_t tMs, const std::string &which)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_ || !anchorJobs_.is_open()) {
        return "";
    }
    ++anchorJobSeq_;
    std::ostringstream id;
    id << "anchor-job-" << tMs << "-" << anchorJobSeq_;
    std::ostringstream oss;
    oss << "{\"job_id\":\"" << Esc(id.str()) << "\",\"t_ms\":" << tMs << ",\"which\":\"" << Esc(which)
        << "\",\"status\":\"queued\"}";
    AppendLine(anchorJobs_, oss.str());
    PendingAnchorJob job;
    job.job_id = id.str();
    job.which = which;
    job.t_ms = tMs;
    pendingAnchorJobs_.push_back(job);
    return id.str();
}

bool ProductStore::PopQueuedAnchorJob(std::string *jobId, std::string *which, int64_t *tMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingAnchorJobs_.empty()) {
        return false;
    }
    const PendingAnchorJob job = pendingAnchorJobs_.front();
    pendingAnchorJobs_.pop_front();
    if (jobId) {
        *jobId = job.job_id;
    }
    if (which) {
        *which = job.which;
    }
    if (tMs) {
        *tMs = job.t_ms;
    }
    return true;
}

void ProductStore::AppendAnchorJobStatus(const std::string &jobId, const std::string &status,
    const std::string &detail)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!anchorJobs_.is_open()) {
        return;
    }
    std::ostringstream oss;
    oss << "{\"job_id\":\"" << Esc(jobId) << "\",\"status\":\"" << Esc(status) << "\",\"detail\":\"" << Esc(detail)
        << "\"}";
    AppendLine(anchorJobs_, oss.str());
}

bool ProductStore::SaveTheta(const Theta &theta) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty()) {
        return false;
    }
    std::ofstream out(root_ + "/theta.json", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "{\n"
        << "  \"coordinate_system\": \"WGS84\",\n"
        << "  \"enter_leave\": " << theta.enter_leave << ",\n"
        << "  \"exit_leave\": " << theta.exit_leave << ",\n"
        << "  \"min_evidence\": " << theta.min_evidence << ",\n"
        << "  \"w_walk\": " << theta.w_walk << ",\n"
        << "  \"w_pdr\": " << theta.w_pdr << ",\n"
        << "  \"w_geo\": " << theta.w_geo << ",\n"
        << "  \"w_wifi\": " << theta.w_wifi << ",\n"
        << "  \"w_cell\": " << theta.w_cell << ",\n"
        << "  \"w_ble\": " << theta.w_ble << ",\n"
        << "  \"w_radio\": " << (theta.w_wifi + theta.w_cell + theta.w_ble) << ",\n"
        << "  \"w_time\": " << theta.w_time << ",\n"
        << "  \"w_baro\": " << theta.w_baro << ",\n"
        << "  \"weekday_leave_home_hour\": " << theta.weekday_leave_home_hour << ",\n"
        << "  \"weekday_leave_company_hour\": " << theta.weekday_leave_company_hour << ",\n"
        << "  \"leave_window_min\": " << theta.leave_window_min << ",\n"
        << "  \"arm_delay_s\": " << theta.arm_delay_s << ",\n"
        << "  \"hsmm_preleave_min_s\": " << theta.hsmm_preleave_min_s << ",\n"
        << "  \"hsmm_preleave_mean_s\": " << theta.hsmm_preleave_mean_s << ",\n"
        << "  \"hsmm_preleave_max_s\": " << theta.hsmm_preleave_max_s << ",\n"
        << "  \"hsmm_leaving_min_s\": " << theta.hsmm_leaving_min_s << ",\n"
        << "  \"hsmm_leaving_mean_s\": " << theta.hsmm_leaving_mean_s << ",\n"
        << "  \"hsmm_leaving_max_s\": " << theta.hsmm_leaving_max_s << ",\n"
        << "  \"hsmm_max_gap_s\": " << theta.hsmm_max_gap_s << ",\n"
        << "  \"lead_min_s\": " << theta.lead_min_s << ",\n"
        << "  \"lead_max_s\": " << theta.lead_max_s << ",\n"
        << "  \"away_confirm_s\": " << theta.away_confirm_s << ",\n"
        << "  \"min_away_s\": " << theta.min_away_s << ",\n"
        << "  \"push_cooldown_s\": " << theta.push_cooldown_s << ",\n"
        << "  \"max_gps_acc_m\": " << theta.max_gps_acc_m << ",\n"
        << "  \"allow_network_dwell_acc_m\": " << theta.allow_network_dwell_acc_m << ",\n"
        << "  \"focus_side\": \"" << theta.focus_side << "\"\n"
        << "}\n";
    return out.good();
}

bool ProductStore::SaveAnchors(const AnchorSet &anchors) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty()) {
        return false;
    }
    std::ofstream out(root_ + "/anchors.json", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << std::setprecision(15) << "{\n"
        << "  \"coordinate_system\": \"" << Esc(anchors.coordinate_system) << "\",\n"
        << "  \"home\": {\"id\":\"" << Esc(anchors.home.id) << "\",\"lat\":" << anchors.home.lat
        << ",\"lon\":" << anchors.home.lon << ",\"r_in_m\":" << anchors.home.r_in_m
        << ",\"r_out_m\":" << anchors.home.r_out_m << ",\"method\":\"" << Esc(anchors.home.method) << "\"},\n"
        << "  \"company\": {\"id\":\"" << Esc(anchors.company.id) << "\",\"lat\":" << anchors.company.lat
        << ",\"lon\":" << anchors.company.lon << ",\"r_in_m\":" << anchors.company.r_in_m
        << ",\"r_out_m\":" << anchors.company.r_out_m << ",\"method\":\"" << Esc(anchors.company.method)
        << "\"},\n"
        << "  \"updated_at\": \"" << Esc(anchors.updated_at) << "\",\n"
        << "  \"notes\": \"" << Esc(anchors.notes) << "\"\n"
        << "}\n";
    return out.good();
}

bool ProductStore::SaveRadioSoftJson(const std::string &json) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty()) {
        return false;
    }
    std::ofstream out(root_ + "/radio_soft.json", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << json;
    if (!json.empty() && json.back() != '\n') {
        out << '\n';
    }
    return out.good();
}

bool ProductStore::LoadRadioSoftJson(std::string *out) const
{
    if (out == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty()) {
        return false;
    }
    std::ifstream in(root_ + "/radio_soft.json", std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return !out->empty();
}

}  // namespace commute_sa
