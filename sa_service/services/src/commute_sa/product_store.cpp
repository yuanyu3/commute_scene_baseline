#include "commute_sa/product_store.h"

#include "commute_sa/anchors.h"

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
    inited_ = leaveEpisodes_.is_open() && paramChanges_.is_open() && personalizeJobs_.is_open() &&
        auditLog_.is_open() && anchorJobs_.is_open();
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
    tStarOutsideMs_ = 0;
    std::ostringstream oss;
    oss << std::setprecision(8)
        << "{\"type\":\"push\",\"t_push_ms\":" << tPushMs << ",\"intent\":\"" << Esc(intent) << "\",\"scene\":\""
        << Esc(scene) << "\",\"score_home\":" << scoreHome << ",\"score_company\":" << scoreCompany
        << ",\"dist_home_m\":" << (hasDistHome ? distHomeM : -1.0) << ",\"walking\":" << (walking ? "true" : "false")
        << ",\"home_relation\":\"" << Esc(homeRelation) << "\",\"eta_leave_s\":" << etaLeaveS
        << ",\"theta\":{\"enter_leave\":" << enterLeave << ",\"min_evidence\":" << minEvidence << "}}";
    AppendLine(leaveEpisodes_, oss.str());
}

void ProductStore::ObserveLeaveProgress(int64_t tMs, const std::string &homeRelation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (activePushMs_ <= 0 || tStarOutsideMs_ > 0) {
        return;
    }
    if (homeRelation == "OUTSIDE") {
        tStarOutsideMs_ = tMs;
    }
}

void ProductStore::AppendLeaveLabel(int64_t tLabelMs, int64_t tPushMs, const std::string &label,
    const std::string &homeRelation, bool hasDistHome, double distHomeM)
{
    std::lock_guard<std::mutex> lock(mutex_);
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
        tStarOutsideMs_ = 0;
    }
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
    return id.str();
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
    return id.str();
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
        << "  \"w_radio\": " << theta.w_radio << ",\n"
        << "  \"w_time\": " << theta.w_time << ",\n"
        << "  \"weekday_leave_home_hour\": " << theta.weekday_leave_home_hour << ",\n"
        << "  \"weekday_leave_company_hour\": " << theta.weekday_leave_company_hour << ",\n"
        << "  \"leave_window_min\": " << theta.leave_window_min << ",\n"
        << "  \"arm_delay_s\": " << theta.arm_delay_s << ",\n"
        << "  \"lead_min_s\": " << theta.lead_min_s << ",\n"
        << "  \"lead_max_s\": " << theta.lead_max_s << ",\n"
        << "  \"away_confirm_s\": " << theta.away_confirm_s << ",\n"
        << "  \"min_away_s\": " << theta.min_away_s << ",\n"
        << "  \"push_cooldown_s\": " << theta.push_cooldown_s << ",\n"
        << "  \"max_gps_acc_m\": " << theta.max_gps_acc_m << ",\n"
        << "  \"allow_network_dwell_acc_m\": " << theta.allow_network_dwell_acc_m << "\n"
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

}  // namespace commute_sa
