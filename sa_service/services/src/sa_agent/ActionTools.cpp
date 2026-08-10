/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Jiuwen action tools for θ personalizer.
 */
#include "sa_agent/ActionTools.h"

#include "commute_sa/action_ops.h"

#include "ErrorCode.h"
#include "ResourceManager.h"
#include "Tool.h"

#include <memory>

namespace sa_agent {
namespace {

using jiuwen::ErrorCode;
using jiuwen::ResourceManager;
using jiuwen::Tool;
using jiuwen::ToolInfo;

class ActionToolBase : public Tool {
public:
    using Handler = std::string (*)(const std::string &);

    ActionToolBase(const ToolInfo &info, Handler handler) : Tool(info), handler_(handler) {}

    std::string invoke(const std::string &params) override
    {
        const std::string raw =
            handler_ ? handler_(params) : "{\"ok\":false,\"error\":\"null handler\"}";
        return std::string("[{\"type\":\"json\",\"value\":") + raw + "}]";
    }

private:
    Handler handler_ = nullptr;
};

std::string CallApplyThetaDelta(const std::string &params)
{
    return commute_sa::ApplyThetaDeltaAction(params);
}

std::string CallWriteAudit(const std::string &params)
{
    return commute_sa::WriteAuditAction(params);
}

std::string CallRequestAnchorReestimate(const std::string &params)
{
    return commute_sa::RequestAnchorReestimateAction(params);
}

std::string CallGetParamLimits(const std::string & /*params*/)
{
    return std::string("{\"ok\":true,\"param_limits\":") + commute_sa::GetParamLimitsJson() + "}";
}

std::string CallEvaluateThetaOnHistory(const std::string &params)
{
    return commute_sa::EvaluateThetaOnHistoryAction(params);
}

std::string CallBeginThetaTrial(const std::string &params)
{
    return commute_sa::BeginThetaTrialAction(params);
}

std::string CallRevertThetaTrial(const std::string &params)
{
    return commute_sa::RevertThetaTrialAction(params);
}

std::string CallCommitThetaTrial(const std::string &params)
{
    return commute_sa::CommitThetaTrialAction(params);
}

ErrorCode RegisterOne(const char *name, const char *desc,
    const std::vector<std::tuple<std::string, std::string, std::string, bool>> &params,
    ActionToolBase::Handler handler)
{
    const ToolInfo info = ToolInfo::createToolInfo(name, desc, params);
    return ResourceManager::RegisterTool(info, [info, handler]() {
        return std::make_shared<ActionToolBase>(info, handler);
    });
}

}  // namespace

const std::vector<std::string> &ActionToolNames()
{
    static const std::vector<std::string> kNames = {
        "apply_theta_delta",
        "write_audit",
        "request_anchor_reestimate",
        "get_param_limits",
        "evaluate_theta_on_history",
        "begin_theta_trial",
        "revert_theta_trial",
        "commit_theta_trial",
    };
    return kNames;
}

std::vector<std::string> RegisterActionTools()
{
    (void)ResourceManager::GetInstance();

    RegisterOne("apply_theta_delta",
        "Apply one clipped theta/fence delta and persist (param_changes + theta.json/anchors.json)",
        {{"param", "enter_leave|exit_leave|w_walk|w_wifi|w_cell|w_ble|w_radio|min_evidence|weekday_leave_home_hour|"
                   "weekday_leave_company_hour|arm_delay_s|lead_min_s|lead_max_s|home.r_in_m|company.r_in_m",
             "string", true},
            {"delta", "Signed delta; clipped to param step", "number", true},
            {"reason", "Short evidence-based reason", "string", false}},
        &CallApplyThetaDelta);

    RegisterOne("write_audit", "Append personalizer audit entry (audit.jsonl), including no_op",
        {{"message", "Human-readable summary / no_op reason", "string", true},
            {"changes", "Optional JSON object of proposed or applied changes", "object", false}},
        &CallWriteAudit);

    RegisterOne("request_anchor_reestimate",
        "Queue offline home/company anchor re-inference job (anchor_reestimate_jobs.jsonl)",
        {{"which", "home|company|both", "string", true}}, &CallRequestAnchorReestimate);

    RegisterOne("get_param_limits", "Return min/max/step for writable params", {}, &CallGetParamLimits);

    RegisterOne("evaluate_theta_on_history",
        "Score current θ on historical leave_episodes (counterfactual push/lead). Higher score is better.",
        {{"since_ms", "optional epoch ms lower bound", "integer", false},
            {"limit", "max episodes, default 30", "integer", false}},
        &CallEvaluateThetaOnHistory);

    RegisterOne("begin_theta_trial",
        "Snapshot θ before try→evaluate→revert/commit loop", {}, &CallBeginThetaTrial);

    RegisterOne("revert_theta_trial", "Restore θ to begin_theta_trial snapshot", {}, &CallRevertThetaTrial);

    RegisterOne("commit_theta_trial", "Keep current θ and clear trial snapshot", {}, &CallCommitThetaTrial);

    return ActionToolNames();
}

}  // namespace sa_agent
