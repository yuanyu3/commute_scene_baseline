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
    };
    return kNames;
}

std::vector<std::string> RegisterActionTools()
{
    (void)ResourceManager::GetInstance();

    RegisterOne("apply_theta_delta",
        "Apply one clipped theta/fence delta and persist (param_changes + theta.json/anchors.json)",
        {{"param", "enter_leave|exit_leave|w_walk|w_radio|min_evidence|weekday_leave_home_hour|"
                   "weekday_leave_company_hour|arm_delay_s|home.r_in_m|company.r_in_m",
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

    return ActionToolNames();
}

}  // namespace sa_agent
