#include "register_tools.h"

#include "commute_sa/action_ops.h"
#include "commute_sa/evidence_query.h"

#include "ErrorCode.h"
#include "ResourceManager.h"
#include "Tool.h"

#include <memory>
#include <vector>

namespace personalizer {
namespace {

using jiuwen::ErrorCode;
using jiuwen::ResourceManager;
using jiuwen::Tool;
using jiuwen::ToolInfo;

class FnTool : public Tool {
public:
    using Handler = std::string (*)(const std::string &);
    FnTool(const ToolInfo &info, Handler h) : Tool(info), h_(h) {}
    std::string invoke(const std::string &params) override
    {
        const std::string raw = h_ ? h_(params) : "{\"ok\":false,\"error\":\"null\"}";
        // jiuwen McpPrivateCustomClient expects: [{"type":"json|text","value":...}]
        return std::string("[{\"type\":\"json\",\"value\":") + raw + "}]";
    }

private:
    Handler h_ = nullptr;
};

ErrorCode Reg(const char *name, const char *desc,
    const std::vector<std::tuple<std::string, std::string, std::string, bool>> &params, FnTool::Handler h)
{
    const ToolInfo info = ToolInfo::createToolInfo(name, desc, params);
    return ResourceManager::RegisterTool(info, [info, h]() { return std::make_shared<FnTool>(info, h); });
}

std::string GetTheta(const std::string &)
{
    return commute_sa::EvidenceQuery::GetInstance().GetThetaJson();
}
std::string GetAnchors(const std::string &)
{
    return commute_sa::EvidenceQuery::GetInstance().GetAnchorsJson();
}
std::string GetErrorStats(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetErrorStatsJson(p);
}
std::string GetLeaveEpisode(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveEpisodeJson(p);
}
std::string GetLeaveSamples(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveWindowSamplesJson(p);
}
std::string GetSensorSummary(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveSensorSummaryJson(p);
}
std::string ApplyDelta(const std::string &p)
{
    return commute_sa::ApplyThetaDeltaAction(p);
}
std::string WriteAudit(const std::string &p)
{
    return commute_sa::WriteAuditAction(p);
}
std::string ReqAnchor(const std::string &p)
{
    return commute_sa::RequestAnchorReestimateAction(p);
}
std::string GetLimits(const std::string &)
{
    return std::string("{\"ok\":true,\"param_limits\":") + commute_sa::GetParamLimitsJson() + "}";
}
std::string EvalHistory(const std::string &p)
{
    return commute_sa::EvaluateThetaOnHistoryAction(p);
}
std::string BeginTrial(const std::string &p)
{
    return commute_sa::BeginThetaTrialAction(p);
}
std::string RevertTrial(const std::string &p)
{
    return commute_sa::RevertThetaTrialAction(p);
}
std::string CommitTrial(const std::string &p)
{
    return commute_sa::CommitThetaTrialAction(p);
}

}  // namespace

std::vector<std::string> RegisterPersonalizerTools()
{
    (void)ResourceManager::GetInstance();
    Reg("get_theta", "Return current theta.json", {}, &GetTheta);
    Reg("get_anchors", "Return anchors.json", {}, &GetAnchors);
    Reg("get_error_stats", "Aggregate leave episode error stats",
        {{"since_ms", "optional", "integer", false}, {"scene", "optional", "string", false}}, &GetErrorStats);
    Reg("get_leave_episode", "Push+label rows for one episode",
        {{"t_push_ms", "optional latest", "integer", false}}, &GetLeaveEpisode);
    Reg("get_leave_window_samples", "Sparse GPS samples after push",
        {{"t_push_ms", "optional", "integer", false}, {"limit", "optional", "integer", false}}, &GetLeaveSamples);
    Reg("get_leave_sensor_summary", "Semantic wifi/cell/gps/mag summary around leave push",
        {{"t_push_ms", "optional", "integer", false}, {"t_center_ms", "optional", "integer", false},
            {"before_s", "optional", "integer", false}, {"after_s", "optional", "integer", false},
            {"session_dir", "optional", "string", false}},
        &GetSensorSummary);
    Reg("get_param_limits", "Param min/max/step", {}, &GetLimits);
    Reg("evaluate_theta_on_history", "Score current θ on leave history (higher better)",
        {{"since_ms", "optional", "integer", false}, {"limit", "optional", "integer", false}}, &EvalHistory);
    Reg("begin_theta_trial", "Snapshot θ before try/eval loop", {}, &BeginTrial);
    Reg("revert_theta_trial", "Restore θ snapshot", {}, &RevertTrial);
    Reg("commit_theta_trial", "Keep current θ, end trial", {}, &CommitTrial);
    Reg("apply_theta_delta", "Apply one clipped theta/fence delta",
        {{"param", "name", "string", true}, {"delta", "signed", "number", true},
            {"reason", "evidence reason", "string", false}},
        &ApplyDelta);
    Reg("write_audit", "Append audit entry / no_op",
        {{"message", "text", "string", true}, {"changes", "object", "object", false}}, &WriteAudit);
    Reg("request_anchor_reestimate", "Queue anchor re-inference",
        {{"which", "home|company|both", "string", true}}, &ReqAnchor);

    return {"get_theta", "get_anchors", "get_error_stats", "get_leave_episode", "get_leave_window_samples",
        "get_leave_sensor_summary", "get_param_limits", "evaluate_theta_on_history", "begin_theta_trial",
        "revert_theta_trial", "commit_theta_trial", "apply_theta_delta", "write_audit", "request_anchor_reestimate"};
}

}  // namespace personalizer
