#include "commute_sa/action_ops.h"

#include "commute_sa/anchor_reestimate.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"
#include "commute_sa/personalization_policy.h"
#include "commute_sa/theta.h"
#include "commute_sa/theta_eval.h"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <limits>
#include <sstream>
#include <vector>

namespace commute_sa {
namespace {

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string Esc(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') {
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

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') {
        return false;
    }
    ++i;
    std::string val;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            val.push_back(json[i + 1]);
            i += 2;
            continue;
        }
        val.push_back(json[i++]);
    }
    *out = val;
    return true;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    char *end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) {
        return false;
    }
    *out = v;
    return true;
}

bool ExtractBool(const std::string &json, const char *key, bool *out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return false;
    if (json.compare(pos, 4, "true") == 0) { *out = true; return true; }
    if (json.compare(pos, 5, "false") == 0) { *out = false; return true; }
    return false;
}

/** Extract raw object/string value after key (for changes blob). */
std::string ExtractRawAfterKey(const std::string &json, const char *key)
{
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return "{}";
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return "{}";
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (i >= json.size()) {
        return "{}";
    }
    if (json[i] == '{') {
        int depth = 0;
        size_t j = i;
        for (; j < json.size(); ++j) {
            if (json[j] == '{') {
                ++depth;
            } else if (json[j] == '}') {
                --depth;
                if (depth == 0) {
                    return json.substr(i, j - i + 1);
                }
            }
        }
        return "{}";
    }
    if (json[i] == '"') {
        std::string s;
        ExtractString(json, key, &s);
        return std::string("\"") + Esc(s) + "\"";
    }
    // number / bool / null until comma or }
    size_t j = i;
    while (j < json.size() && json[j] != ',' && json[j] != '}') {
        ++j;
    }
    return json.substr(i, j - i);
}

struct ParamLimit {
    double minV = 0;
    double maxV = 1;
    double step = 0.01;
};

bool LookupLimit(const std::string &param, ParamLimit *lim)
{
    if (lim == nullptr) {
        return false;
    }
    if (param == "enter_leave") {
        *lim = {0.40, 0.85, 0.03};
        return true;
    }
    if (param == "exit_leave") {
        *lim = {0.20, 0.70, 0.03};
        return true;
    }
    if (param == "w_walk") {
        *lim = {0.0, 0.50, 0.05};
        return true;
    }
    if (param == "w_pdr") {
        *lim = {0.0, 0.40, 0.05};
        return true;
    }
    if (param == "w_geo") {
        *lim = {0.05, 0.40, 0.05};
        return true;
    }
    if (param == "w_wifi") {
        *lim = {0.0, 0.40, 0.02};
        return true;
    }
    if (param == "w_cell") {
        *lim = {0.0, 0.30, 0.02};
        return true;
    }
    if (param == "w_ble") {
        *lim = {0.0, 0.20, 0.02};
        return true;
    }
    if (param == "w_radio") {
        *lim = {0.0, 0.90, 0.05};
        return true;
    }
    if (param == "w_time") {
        *lim = {0.05, 0.35, 0.05};
        return true;
    }
    if (param == "w_baro") {
        *lim = {0.0, 0.40, 0.05};
        return true;
    }
    if (param == "weekday_leave_home_hour") {
        *lim = {0.0, 24.0, 0.10};
        return true;
    }
    if (param == "weekday_leave_company_hour") {
        *lim = {11.0, 21.0, 0.10};
        return true;
    }
    if (param == "arm_delay_s") {
        *lim = {0.0, 90.0, 5.0};
        return true;
    }
    if (param == "hsmm_preleave_min_s") {
        *lim = {0.0, 120.0, 5.0};
        return true;
    }
    if (param == "hsmm_preleave_mean_s") {
        *lim = {20.0, 240.0, 10.0};
        return true;
    }
    if (param == "hsmm_preleave_max_s") {
        *lim = {60.0, 900.0, 30.0};
        return true;
    }
    if (param == "hsmm_leaving_min_s") {
        *lim = {0.0, 120.0, 5.0};
        return true;
    }
    if (param == "hsmm_leaving_mean_s") {
        *lim = {20.0, 360.0, 10.0};
        return true;
    }
    if (param == "hsmm_leaving_max_s") {
        *lim = {60.0, 1200.0, 30.0};
        return true;
    }
    if (param == "lead_min_s") {
        *lim = {0.0, 600.0, 15.0};
        return true;
    }
    if (param == "lead_max_s") {
        *lim = {30.0, 900.0, 30.0};
        return true;
    }
    if (param == "baro_min_descent_m") {
        *lim = {2.0, 40.0, 2.0};
        return true;
    }
    return false;
}

double ClipDeltaToStep(double delta, double step)
{
    if (step <= 0.0) {
        return delta;
    }
    if (std::fabs(delta) <= step) {
        return delta;
    }
    return (delta > 0.0) ? step : -step;
}

double ClipDeltaToRange(double oldValue, double delta, const ParamLimit &lim)
{
    const double target = std::max(lim.minV, std::min(lim.maxV, oldValue + delta));
    return target - oldValue;
}

bool ReadParamValue(const Theta &t, const std::string &param, double *out)
{
    if (out == nullptr) {
        return false;
    }
    if (param == "enter_leave") {
        *out = t.enter_leave;
        return true;
    }
    if (param == "exit_leave") {
        *out = t.exit_leave;
        return true;
    }
    if (param == "w_walk") {
        *out = t.w_walk;
        return true;
    }
    if (param == "w_pdr") {
        *out = t.w_pdr;
        return true;
    }
    if (param == "w_geo") {
        *out = t.w_geo;
        return true;
    }
    if (param == "w_wifi") {
        *out = t.w_wifi;
        return true;
    }
    if (param == "w_cell") {
        *out = t.w_cell;
        return true;
    }
    if (param == "w_ble") {
        *out = t.w_ble;
        return true;
    }
    if (param == "w_radio") {
        *out = t.w_wifi + t.w_cell + t.w_ble;
        return true;
    }
    if (param == "w_time") {
        *out = t.w_time;
        return true;
    }
    if (param == "w_baro") {
        *out = t.w_baro;
        return true;
    }
    if (param == "weekday_leave_home_hour") {
        *out = t.weekday_leave_home_hour;
        return true;
    }
    if (param == "weekday_leave_company_hour") {
        *out = t.weekday_leave_company_hour;
        return true;
    }
    if (param == "arm_delay_s") {
        *out = t.arm_delay_s;
        return true;
    }
    if (param == "hsmm_preleave_min_s") {
        *out = t.hsmm_preleave_min_s;
        return true;
    }
    if (param == "hsmm_preleave_mean_s") {
        *out = t.hsmm_preleave_mean_s;
        return true;
    }
    if (param == "hsmm_preleave_max_s") {
        *out = t.hsmm_preleave_max_s;
        return true;
    }
    if (param == "hsmm_leaving_min_s") {
        *out = t.hsmm_leaving_min_s;
        return true;
    }
    if (param == "hsmm_leaving_mean_s") {
        *out = t.hsmm_leaving_mean_s;
        return true;
    }
    if (param == "hsmm_leaving_max_s") {
        *out = t.hsmm_leaving_max_s;
        return true;
    }
    if (param == "lead_min_s") {
        *out = t.lead_min_s;
        return true;
    }
    if (param == "lead_max_s") {
        *out = t.lead_max_s;
        return true;
    }
    if (param == "baro_min_descent_m") {
        *out = t.baro_min_descent_m;
        return true;
    }
    return false;
}

std::mutex gPolicyTrialMutex;
bool gPolicyTrialActive = false;
PersonalizationPolicy gPolicyTrialSnapshot;
bool gPolicyCandidateApplied = false;
double gPolicyBaselineScore = std::numeric_limits<double>::quiet_NaN();
double gPolicyCandidateScore = std::numeric_limits<double>::quiet_NaN();
int gPolicyBaselineMissed = -1;
int gPolicyCandidateMissed = -1;

std::string RootDir();

PersonalizationPolicy LoadLivePolicy()
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        return BaselineRuntime::GetInstance().Engine()->GetPersonalizationPolicy();
    }
    PersonalizationPolicy policy = DefaultPersonalizationPolicy();
    LoadPersonalizationPolicyFromFile(RootDir() + "/policy.json", &policy, nullptr);
    return policy;
}

bool PersistPolicy(const PersonalizationPolicy &policy, std::string *err)
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        if (!BaselineRuntime::GetInstance().ApplyPersonalizationPolicyAndPersist(policy)) {
            if (err) *err = "live policy persist failed";
            return false;
        }
        return true;
    }
    return SavePersonalizationPolicyToFile(RootDir() + "/policy.json", policy, err);
}

std::string RootDir()
{
    const std::string &ps = ProductStore::GetInstance().RootDir();
    if (!ps.empty()) {
        return ps;
    }
    const std::string &eq = EvidenceQuery::GetInstance().RootDir();
    if (!eq.empty()) {
        return eq;
    }
    return "/data/service/el1/public/commuteagentservice";
}

}  // namespace

std::string GetParamLimitsJson()
{
    return R"({"range":"bounded","note":"each apply is clipped to ±step and absolute min/max",)"
           R"("enter_leave":{"min":0.40,"max":0.85,"step":0.03},"exit_leave":{"min":0.20,"max":0.70,"step":0.03},)"
           R"("w_walk":{"min":0,"max":0.50,"step":0.05},"w_pdr":{"min":0,"max":0.40,"step":0.05},)"
           R"("w_geo":{"min":0.05,"max":0.40,"step":0.05},"w_wifi":{"min":0,"max":0.40,"step":0.02},)"
           R"("w_cell":{"min":0,"max":0.30,"step":0.02},"w_ble":{"min":0,"max":0.20,"step":0.02},)"
           R"("w_radio":{"min":0,"max":0.90,"step":0.05,"note":"legacy; prefer split channels"},)"
           R"("w_time":{"min":0.05,"max":0.35,"step":0.05},"w_baro":{"min":0,"max":0.40,"step":0.05},)"
           R"("weekday_leave_home_hour":{"min":0,"max":24,"step":0.10},)"
           R"("weekday_leave_company_hour":{"min":11,"max":21,"step":0.10},"arm_delay_s":{"min":0,"max":90,"step":5},)"
           R"("hsmm_preleave_min_s":{"min":0,"max":120,"step":5},"hsmm_preleave_mean_s":{"min":20,"max":240,"step":10},)"
           R"("hsmm_preleave_max_s":{"min":60,"max":900,"step":30},"hsmm_leaving_min_s":{"min":0,"max":120,"step":5},)"
           R"("hsmm_leaving_mean_s":{"min":20,"max":360,"step":10},"hsmm_leaving_max_s":{"min":60,"max":1200,"step":30},)"
           R"("lead_min_s":{"min":0,"max":600,"step":15},"lead_max_s":{"min":30,"max":900,"step":30},)"
           R"("baro_min_descent_m":{"min":2,"max":40,"step":2}})";
}

std::string ApplyThetaDeltaAction(const std::string &paramsJson)
{
    // Serialize applies: LLM may fire multiple apply_theta_delta in one turn.
    static std::mutex applyMutex;
    std::lock_guard<std::mutex> lock(applyMutex);

    std::string param;
    double delta = 0.0;
    std::string reason;
    if (!ExtractString(paramsJson, "param", &param) || param.empty()) {
        return "{\"ok\":false,\"error\":\"missing param\"}";
    }
    if (!ExtractNumber(paramsJson, "delta", &delta)) {
        return "{\"ok\":false,\"error\":\"missing delta\"}";
    }
    ExtractString(paramsJson, "reason", &reason);
    if (reason.empty()) {
        reason = "agent_apply";
    }

    ParamLimit lim {};
    if (!LookupLimit(param, &lim)) {
        return "{\"ok\":false,\"error\":\"unsupported param\",\"param\":\"" + Esc(param) +
            "\",\"param_limits\":" + GetParamLimitsJson() + "}";
    }
    const double rawDelta = delta;
    delta = ClipDeltaToStep(delta, lim.step);

    double oldV = 0.0;
    double newV = 0.0;
    std::string err;
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        ReadParamValue(BaselineRuntime::GetInstance().Engine()->GetTheta(), param, &oldV);
        delta = ClipDeltaToRange(oldV, delta, lim);
        if (!BaselineRuntime::GetInstance().ApplyThetaDeltaAndPersist(param, delta, reason)) {
            return "{\"ok\":false,\"error\":\"ApplyThetaDeltaAndPersist failed\",\"param\":\"" + Esc(param) + "\"}";
        }
        ReadParamValue(BaselineRuntime::GetInstance().Engine()->GetTheta(), param, &newV);
    } else {
        const std::string root = RootDir();
        Theta t = DefaultTheta();
        LoadThetaFromFile(root + "/theta.json", &t, nullptr);
        if (!ReadParamValue(t, param, &oldV)) {
            return "{\"ok\":false,\"error\":\"cannot read param\"}";
        }
        delta = ClipDeltaToRange(oldV, delta, lim);
        if (!ApplyThetaDelta(&t, param, delta, &err)) {
            return "{\"ok\":false,\"error\":\"" + Esc(err.empty() ? "ApplyThetaDelta failed" : err) + "\"}";
        }
        ReadParamValue(t, param, &newV);
        ProductStore::GetInstance().AppendParamChange(NowMs(), param, oldV, newV, reason);
        if (!ProductStore::GetInstance().SaveTheta(t)) {
            return "{\"ok\":false,\"error\":\"SaveTheta failed\"}";
        }
    }

    std::ostringstream oss;
    oss << "{\"ok\":true,\"param\":\"" << Esc(param) << "\",\"delta_requested\":" << rawDelta
        << ",\"delta_applied\":" << delta << ",\"old\":" << oldV << ",\"new\":" << newV << ",\"reason\":\""
        << Esc(reason) << "\",\"param_limits\":" << GetParamLimitsJson() << "}";
    return oss.str();
}

std::string WriteAuditAction(const std::string &paramsJson)
{
    std::string message;
    ExtractString(paramsJson, "message", &message);
    if (message.empty()) {
        ExtractString(paramsJson, "note", &message);
    }
    if (message.empty()) {
        return "{\"ok\":false,\"error\":\"missing message\"}";
    }
    const std::string changes = ExtractRawAfterKey(paramsJson, "changes");
    const std::string jobId = ProductStore::GetInstance().AppendAudit(NowMs(), message, changes);
    if (jobId.empty()) {
        return "{\"ok\":false,\"error\":\"AppendAudit failed (ProductStore not inited?)\"}";
    }
    return "{\"ok\":true,\"audit_id\":\"" + Esc(jobId) + "\",\"message\":\"" + Esc(message) + "\"}";
}

std::string RequestAnchorReestimateAction(const std::string &paramsJson)
{
    std::string which = "both";
    ExtractString(paramsJson, "which", &which);
    if (which != "home" && which != "company" && which != "both") {
        return "{\"ok\":false,\"error\":\"which must be home|company|both\"}";
    }
    const std::string jobId = ProductStore::GetInstance().AppendAnchorReestimateJob(NowMs(), which);
    if (jobId.empty()) {
        return "{\"ok\":false,\"error\":\"AppendAnchorReestimateJob failed\"}";
    }
    // Execute immediately (on-device / host). Extra queued jobs also drained on DAY_END.
    const std::string exec = ProcessQueuedAnchorReestimateJobs(RootDir(), 2);
    return "{\"ok\":true,\"job_id\":\"" + Esc(jobId) + "\",\"which\":\"" + Esc(which) +
        "\",\"status\":\"queued_then_run\",\"execution\":" + exec + "}";
}

std::string EvaluateThetaOnHistoryAction(const std::string &paramsJson)
{
    double since = 0.0;
    double limit = 30.0;
    ExtractNumber(paramsJson, "since_ms", &since);
    ExtractNumber(paramsJson, "limit", &limit);
    if (limit < 1.0) {
        limit = 1.0;
    }
    if (limit > 100.0) {
        limit = 100.0;
    }
    const std::string root = RootDir();
    Theta t = DefaultTheta();
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        t = BaselineRuntime::GetInstance().Engine()->GetTheta();
    } else {
        LoadThetaFromFile(root + "/theta.json", &t, nullptr);
    }
    std::string out = EvaluateThetaOnHistoryJson(root, t, static_cast<int64_t>(since), static_cast<int>(limit));
    // Annotate trial state for the agent loop.
    const std::string trial = HasActiveThetaTrial() ? "true" : "false";
    if (out.size() > 1 && out.back() == '}') {
        out.pop_back();
        out += ",\"trial_active\":" + trial + "}";
    }
    return out;
}

std::string BeginThetaTrialAction(const std::string & /*paramsJson*/)
{
    std::string err;
    if (!BeginThetaTrial(&err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err.empty() ? "begin failed" : err) + "\"}";
    }
    return "{\"ok\":true,\"trial_active\":true,\"note\":\"snapshot taken; apply_theta_delta then "
           "evaluate_theta_on_history; revert_theta_trial if score worsens\"}";
}

std::string RevertThetaTrialAction(const std::string & /*paramsJson*/)
{
    std::string err;
    if (!RevertThetaTrial(&err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err.empty() ? "revert failed" : err) + "\"}";
    }
    return "{\"ok\":true,\"trial_active\":false,\"note\":\"theta restored to trial snapshot\"}";
}

std::string CommitThetaTrialAction(const std::string & /*paramsJson*/)
{
    std::string err;
    if (!CommitThetaTrial(&err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err.empty() ? "commit failed" : err) + "\"}";
    }
    return "{\"ok\":true,\"trial_active\":false,\"note\":\"kept current theta (already persisted by "
           "apply_theta_delta)\"}";
}

std::string GetPersonalizationPolicyAction(const std::string & /*paramsJson*/)
{
    return std::string("{\"ok\":true,\"trial_active\":") + (gPolicyTrialActive ? "true" : "false") +
        ",\"policy\":" + PersonalizationPolicyToJson(LoadLivePolicy()) + "}";
}

std::string GetPolicyCatalogAction(const std::string & /*paramsJson*/)
{
    return std::string("{\"ok\":true,\"catalog\":") + PersonalizationPolicyCatalogJson() + "}";
}

std::string BeginPolicyTrialAction(const std::string & /*paramsJson*/)
{
    std::lock_guard<std::mutex> lock(gPolicyTrialMutex);
    if (gPolicyTrialActive) return "{\"ok\":false,\"error\":\"policy trial already active\"}";
    gPolicyTrialSnapshot = LoadLivePolicy();
    gPolicyTrialActive = true;
    gPolicyCandidateApplied = false;
    gPolicyBaselineScore = std::numeric_limits<double>::quiet_NaN();
    gPolicyCandidateScore = std::numeric_limits<double>::quiet_NaN();
    gPolicyBaselineMissed = -1;
    gPolicyCandidateMissed = -1;
    return std::string("{\"ok\":true,\"trial_active\":true,\"snapshot\":") +
        PersonalizationPolicyToJson(gPolicyTrialSnapshot) + "}";
}

std::string ApplyPolicyCandidateAction(const std::string &paramsJson)
{
    std::lock_guard<std::mutex> lock(gPolicyTrialMutex);
    if (!gPolicyTrialActive) return "{\"ok\":false,\"error\":\"begin_policy_trial required\"}";
    std::string name;
    if (!ExtractString(paramsJson, "template_name", &name)) {
        return "{\"ok\":false,\"error\":\"missing template_name\"}";
    }
    if (name != "confirmed_leaving") {
        return "{\"ok\":false,\"error\":\"only confirmed_leaving is in the catalog; live engine ignores policy templates for push\",\"catalog\":" +
            PersonalizationPolicyCatalogJson() + "}";
    }
    PersonalizationPolicy candidate;
    std::string err;
    if (!BuildPolicyTemplate(name, &candidate, &err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err) + "\",\"catalog\":" +
            PersonalizationPolicyCatalogJson() + "}";
    }
    candidate.revision = LoadLivePolicy().revision + 1;
    double value = 0.0;
    bool flag = false;
    std::string text;
    if (ExtractNumber(paramsJson, "probability_threshold", &value)) candidate.probability_threshold = value;
    if (ExtractNumber(paramsJson, "min_duration_s", &value)) candidate.min_duration_s = value;
    if (ExtractNumber(paramsJson, "min_independent_evidence", &value)) candidate.min_independent_evidence = static_cast<int>(value);
    if (ExtractBool(paramsJson, "require_walking", &flag)) candidate.require_walking = flag;
    if (ExtractBool(paramsJson, "require_wifi_detach", &flag)) candidate.require_wifi_detach = flag;
    if (ExtractBool(paramsJson, "require_radio", &flag)) candidate.require_radio = flag;
    if (ExtractBool(paramsJson, "allow_cell_pdr_pair", &flag)) candidate.allow_cell_pdr_pair = flag;
    if (ExtractString(paramsJson, "gps_mode", &text)) candidate.gps_mode = text;
    if (!ValidatePersonalizationPolicy(candidate, &err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err) + "\"}";
    }
    if (!PersistPolicy(candidate, &err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err) + "\"}";
    }
    gPolicyCandidateApplied = true;
    gPolicyCandidateScore = std::numeric_limits<double>::quiet_NaN();
    gPolicyCandidateMissed = -1;
    return std::string("{\"ok\":true,\"trial_active\":true,\"candidate\":") +
        PersonalizationPolicyToJson(candidate) + "}";
}

std::string EvaluatePolicyOnHistoryAction(const std::string &paramsJson)
{
    double limitValue = 5000.0;
    ExtractNumber(paramsJson, "limit", &limitValue);
    const int limit = std::max(1, std::min(20000, static_cast<int>(limitValue)));
    const std::string path = RootDir() + "/policy_history.jsonl";
    std::ifstream in(path);
    if (!in) {
        return "{\"ok\":false,\"error\":\"policy_history.jsonl missing\",\"required_fields\":[\"label\",\"preleave_probability\",\"leaving_probability\",\"hits\",\"walking\",\"wifi_detach\",\"cell_leave\",\"ble_detach\",\"pdr_net_out_m\",\"baro_available\",\"baro_baseline_ready\",\"baro_descent_m\",\"baro_lower_platform\",\"evidence_duration_s\"]}";
    }
    struct EvalRow { PolicyEvidence evidence; double explicit_duration_s = -1.0; double lead_s = -1.0; };
    struct EvalEpisode { std::string label; std::vector<EvalRow> rows; };
    std::map<std::string, EvalEpisode> episodes;
    int n = 0;
    std::string line;
    while (n < limit && std::getline(in, line)) {
        std::string label;
        if (!ExtractString(line, "label", &label)) continue;
        EvalRow row;
        double value = 0.0;
        if (ExtractNumber(line, "t_ms", &value)) row.evidence.t_ms = static_cast<int64_t>(value);
        ExtractNumber(line, "preleave_probability", &row.evidence.preleave_probability);
        ExtractNumber(line, "leaving_probability", &row.evidence.leaving_probability);
        if (ExtractNumber(line, "hits", &value)) row.evidence.baseline_hits = static_cast<int>(value);
        ExtractBool(line, "walking", &row.evidence.walking);
        ExtractNumber(line, "pdr_net_out_m", &row.evidence.pdr_net_out_m);
        ExtractBool(line, "wifi_detach", &row.evidence.wifi_detach);
        ExtractBool(line, "cell_leave", &row.evidence.cell_leave);
        ExtractBool(line, "ble_detach", &row.evidence.ble_detach);
        ExtractBool(line, "geo_outbound", &row.evidence.geo_outbound);
        ExtractBool(line, "has_usable_gps", &row.evidence.has_usable_gps);
        ExtractBool(line, "baro_available", &row.evidence.baro_available);
        ExtractBool(line, "baro_baseline_ready", &row.evidence.baro_baseline_ready);
        ExtractNumber(line, "baro_descent_m", &row.evidence.baro_descent_m);
        ExtractBool(line, "baro_lower_platform", &row.evidence.baro_lower_platform);
        ExtractNumber(line, "evidence_duration_s", &row.explicit_duration_s);
        ExtractNumber(line, "lead_s", &row.lead_s);
        std::string side = "unknown";
        ExtractString(line, "side", &side);
        double outcome = static_cast<double>(row.evidence.t_ms);
        ExtractNumber(line, "outcome_t_ms", &outcome);
        const std::string key = side + ":" + std::to_string(static_cast<int64_t>(outcome)) + ":" + label;
        episodes[key].label = label;
        episodes[key].rows.push_back(row);
        ++n;
    }

    const PersonalizationPolicy policy = LoadLivePolicy();
    int confirmed = 0, falseCases = 0, matchedConfirmed = 0, matchedFalse = 0;
    double score = 0.0;
    for (auto &entry : episodes) {
        auto &episode = entry.second;
        std::sort(episode.rows.begin(), episode.rows.end(), [](const EvalRow &a, const EvalRow &b) {
            return a.evidence.t_ms < b.evidence.t_ms;
        });
        bool matched = false;
        int64_t runStart = 0;
        int64_t previousMatch = 0;
        double matchedLead = -1.0;
        for (const auto &row : episode.rows) {
            if (!MatchPersonalizationPolicy(policy, row.evidence).matched) {
                runStart = 0;
                previousMatch = 0;
                continue;
            }
            if (previousMatch > 0 && row.evidence.t_ms - previousMatch > 15000) runStart = 0;
            if (runStart == 0) runStart = row.evidence.t_ms;
            previousMatch = row.evidence.t_ms;
            const double observedDuration = row.explicit_duration_s >= 0.0 ? row.explicit_duration_s :
                static_cast<double>(row.evidence.t_ms - runStart) / 1000.0;
            if (observedDuration >= policy.min_duration_s) {
                matched = true;
                matchedLead = row.lead_s;
                break;
            }
        }
        if (episode.label == "CONFIRMED_LEAVE" || episode.label == "DEPARTURE_INTENT" ||
            episode.label == "MISSED_LEAVE") {
            ++confirmed;
            if (matched) { ++matchedConfirmed; score += 2.0; } else { score -= 4.0; }
            if (matchedLead >= 0.0) {
                if (matchedLead >= 15.0 && matchedLead <= 300.0) score += 1.0;
                else score -= 0.5;
            }
        } else if (episode.label == "FALSE_PUSH") {
            ++falseCases;
            if (matched) { ++matchedFalse; score -= 3.0; } else { score += 1.0; }
        }
    }
    std::ostringstream out;
    const int missed = confirmed - matchedConfirmed;
    if (gPolicyTrialActive) {
        if (gPolicyCandidateApplied) {
            gPolicyCandidateScore = score;
            gPolicyCandidateMissed = missed;
        } else {
            gPolicyBaselineScore = score;
            gPolicyBaselineMissed = missed;
        }
    }
    out << "{\"ok\":true,\"method\":\"semantic_policy_counterfactual\",\"trial_active\":"
        << (gPolicyTrialActive ? "true" : "false") << ",\"n_samples\":" << n
        << ",\"n_episodes\":" << episodes.size()
        << ",\"confirmed\":" << confirmed << ",\"false_cases\":" << falseCases
        << ",\"matched_confirmed\":" << matchedConfirmed << ",\"matched_false\":" << matchedFalse
        << ",\"missed\":" << missed << ",\"score\":" << score
        << ",\"policy\":" << PersonalizationPolicyToJson(policy) << "}";
    return out.str();
}

std::string RevertPolicyTrialAction(const std::string & /*paramsJson*/)
{
    std::lock_guard<std::mutex> lock(gPolicyTrialMutex);
    if (!gPolicyTrialActive) return "{\"ok\":false,\"error\":\"no active policy trial\"}";
    std::string err;
    if (!PersistPolicy(gPolicyTrialSnapshot, &err)) return "{\"ok\":false,\"error\":\"" + Esc(err) + "\"}";
    gPolicyTrialActive = false;
    gPolicyCandidateApplied = false;
    return std::string("{\"ok\":true,\"trial_active\":false,\"policy\":") +
        PersonalizationPolicyToJson(gPolicyTrialSnapshot) + "}";
}

std::string CommitPolicyTrialAction(const std::string & /*paramsJson*/)
{
    std::lock_guard<std::mutex> lock(gPolicyTrialMutex);
    if (!gPolicyTrialActive) return "{\"ok\":false,\"error\":\"no active policy trial\"}";
    if (!gPolicyCandidateApplied || std::isnan(gPolicyBaselineScore) || std::isnan(gPolicyCandidateScore)) {
        return "{\"ok\":false,\"error\":\"baseline and candidate evaluation required before commit\"}";
    }
    if (gPolicyCandidateScore <= gPolicyBaselineScore || gPolicyCandidateMissed > gPolicyBaselineMissed) {
        std::ostringstream rejected;
        rejected << "{\"ok\":false,\"error\":\"candidate did not improve safely; revert required\""
                 << ",\"baseline_score\":" << gPolicyBaselineScore << ",\"candidate_score\":" << gPolicyCandidateScore
                 << ",\"baseline_missed\":" << gPolicyBaselineMissed << ",\"candidate_missed\":" << gPolicyCandidateMissed << "}";
        return rejected.str();
    }
    gPolicyTrialActive = false;
    gPolicyCandidateApplied = false;
    return std::string("{\"ok\":true,\"trial_active\":false,\"policy\":") +
        PersonalizationPolicyToJson(LoadLivePolicy()) + "}";
}

}  // namespace commute_sa
