#include "commute_sa/action_ops.h"

#include "commute_sa/anchor_reestimate.h"
#include "commute_sa/anchors.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"
#include "commute_sa/theta_eval.h"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <sstream>

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
        *lim = {0.4, 0.85, 0.03};
        return true;
    }
    if (param == "exit_leave") {
        *lim = {0.2, 0.7, 0.03};
        return true;
    }
    if (param == "w_walk") {
        *lim = {0.05, 0.4, 0.05};
        return true;
    }
    if (param == "w_wifi") {
        *lim = {0.0, 0.4, 0.02};
        return true;
    }
    if (param == "w_cell") {
        *lim = {0.0, 0.3, 0.02};
        return true;
    }
    if (param == "w_ble") {
        *lim = {0.0, 0.2, 0.02};
        return true;
    }
    if (param == "w_radio") {
        *lim = {0.05, 0.4, 0.05};
        return true;
    }
    if (param == "min_evidence") {
        *lim = {1.0, 5.0, 1.0};
        return true;
    }
    if (param == "weekday_leave_home_hour") {
        *lim = {5.0, 11.0, 0.083};
        return true;
    }
    if (param == "weekday_leave_company_hour") {
        *lim = {16.0, 21.0, 0.083};
        return true;
    }
    if (param == "arm_delay_s") {
        *lim = {0.0, 90.0, 5.0};
        return true;
    }
    if (param == "lead_min_s") {
        *lim = {30.0, 180.0, 15.0};
        return true;
    }
    if (param == "lead_max_s") {
        *lim = {60.0, 600.0, 30.0};
        return true;
    }
    if (param == "home.r_in_m") {
        *lim = {40.0, 150.0, 5.0};
        return true;
    }
    if (param == "company.r_in_m") {
        *lim = {40.0, 200.0, 5.0};
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
    if (param == "min_evidence") {
        *out = static_cast<double>(t.min_evidence);
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
    if (param == "lead_min_s") {
        *out = t.lead_min_s;
        return true;
    }
    if (param == "lead_max_s") {
        *out = t.lead_max_s;
        return true;
    }
    return false;
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

bool ApplyFenceDelta(const std::string &param, double delta, const std::string &reason, double *oldV, double *newV,
    std::string *err)
{
    ParamLimit lim {};
    if (!LookupLimit(param, &lim)) {
        if (err) {
            *err = "unknown fence param";
        }
        return false;
    }
    delta = ClipDeltaToStep(delta, lim.step);
    const std::string root = RootDir();
    AnchorSet anchors = DefaultAnchors();
    LoadAnchorsFromFile(root + "/anchors.json", &anchors, nullptr);
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        anchors = BaselineRuntime::GetInstance().Engine()->GetAnchors();
    }
    double *target = nullptr;
    if (param == "home.r_in_m") {
        target = &anchors.home.r_in_m;
    } else if (param == "company.r_in_m") {
        target = &anchors.company.r_in_m;
    } else {
        return false;
    }
    *oldV = *target;
    *target = std::max(lim.minV, std::min(lim.maxV, *target + delta));
    *newV = *target;
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        BaselineRuntime::GetInstance().Engine()->SetAnchors(anchors);
    }
    ProductStore::GetInstance().AppendParamChange(NowMs(), param, *oldV, *newV, reason);
    if (!ProductStore::GetInstance().SaveAnchors(anchors)) {
        if (err) {
            *err = "SaveAnchors failed";
        }
        return false;
    }
    return true;
}

}  // namespace

std::string GetParamLimitsJson()
{
    return R"({"enter_leave":{"min":0.4,"max":0.85,"step":0.03},"exit_leave":{"min":0.2,"max":0.7,"step":0.03},)"
           R"("w_walk":{"min":0.05,"max":0.4,"step":0.05},"w_wifi":{"min":0,"max":0.4,"step":0.02},)"
           R"("w_cell":{"min":0,"max":0.3,"step":0.02},"w_ble":{"min":0,"max":0.2,"step":0.02},)"
           R"("w_radio":{"min":0.05,"max":0.4,"step":0.05,"note":"legacy; prefer w_wifi/w_cell/w_ble"},)"
           R"("min_evidence":{"min":1,"max":5,"step":1},"weekday_leave_home_hour":{"min":5,"max":11,"step":0.083},)"
           R"("weekday_leave_company_hour":{"min":16,"max":21,"step":0.083},"arm_delay_s":{"min":0,"max":90,"step":5},)"
           R"("lead_min_s":{"min":30,"max":180,"step":15},"lead_max_s":{"min":60,"max":600,"step":30},)"
           R"("home.r_in_m":{"min":40,"max":150,"step":5},"company.r_in_m":{"min":40,"max":200,"step":5}})";
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
    if (param == "home.r_in_m" || param == "company.r_in_m") {
        std::string err;
        if (!ApplyFenceDelta(param, delta, reason, &oldV, &newV, &err)) {
            return "{\"ok\":false,\"error\":\"" + Esc(err.empty() ? "fence apply failed" : err) + "\"}";
        }
        std::ostringstream oss;
        oss << "{\"ok\":true,\"param\":\"" << Esc(param) << "\",\"delta_requested\":" << rawDelta
            << ",\"delta_applied\":" << delta << ",\"old\":" << oldV << ",\"new\":" << newV
            << ",\"reason\":\"" << Esc(reason) << "\"}";
        return oss.str();
    }

    std::string err;
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        ReadParamValue(BaselineRuntime::GetInstance().Engine()->GetTheta(), param, &oldV);
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

}  // namespace commute_sa
