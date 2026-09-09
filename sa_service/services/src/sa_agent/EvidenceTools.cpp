/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Jiuwen evidence tools for θ personalizer (semantic summaries, not raw dumps).
 */
#include "sa_agent/EvidenceTools.h"

#include "commute_sa/evidence_query.h"

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

class EvidenceToolBase : public Tool {
public:
    using Handler = std::string (*)(const std::string &);

    EvidenceToolBase(const ToolInfo &info, Handler handler) : Tool(info), handler_(handler) {}

    std::string invoke(const std::string &params) override
    {
        const std::string raw =
            handler_ ? handler_(params) : "{\"ok\":false,\"error\":\"null handler\"}";
        return std::string("[{\"type\":\"json\",\"value\":") + raw + "}]";
    }

private:
    Handler handler_ = nullptr;
};

std::string CallGetTheta(const std::string & /*params*/)
{
    return commute_sa::EvidenceQuery::GetInstance().GetThetaJson();
}

std::string CallGetAnchors(const std::string & /*params*/)
{
    return commute_sa::EvidenceQuery::GetInstance().GetAnchorsJson();
}

std::string CallGetErrorStats(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetErrorStatsJson(params);
}

std::string CallGetLeaveEpisode(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveEpisodeJson(params);
}

std::string CallGetLeaveWindowSamples(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveWindowSamplesJson(params);
}

std::string CallGetLeaveSensorSummary(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetLeaveSensorSummaryJson(params);
}

std::string CallGetSemanticTimeline(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetEpisodeSemanticTimelineJson(params);
}

std::string CallGetDynamicDiagnostics(const std::string &params)
{
    return commute_sa::EvidenceQuery::GetInstance().GetEpisodeDynamicDiagnosticsJson(params);
}

ErrorCode RegisterOne(const char *name, const char *desc,
    const std::vector<std::tuple<std::string, std::string, std::string, bool>> &params,
    EvidenceToolBase::Handler handler)
{
    const ToolInfo info = ToolInfo::createToolInfo(name, desc, params);
    return ResourceManager::RegisterTool(info, [info, handler]() {
        return std::make_shared<EvidenceToolBase>(info, handler);
    });
}

}  // namespace

const std::vector<std::string> &EvidenceToolNames()
{
    static const std::vector<std::string> kNames = {
        "get_theta",
        "get_anchors",
        "get_error_stats",
        "get_leave_episode",
        "get_leave_window_samples",
        "get_leave_sensor_summary",
        "get_episode_semantic_timeline",
        "get_episode_dynamic_diagnostics",
    };
    return kNames;
}

std::vector<std::string> RegisterEvidenceTools()
{
    (void)ResourceManager::GetInstance();

    RegisterOne("get_theta", "Return current theta.json from product root", {}, &CallGetTheta);
    RegisterOne("get_anchors", "Return current anchors.json (WGS84 home/company)", {}, &CallGetAnchors);
    RegisterOne("get_error_stats",
        "Aggregate leave_episodes.jsonl: n_push / false_push / confirmed_leave since_ms",
        {{"since_ms", "Only count episodes with t_push_ms >= since_ms (0=all)", "integer", false},
            {"scene", "LEAVING_HOME|LEAVING_COMPANY|ALL (echoed)", "string", false}},
        &CallGetErrorStats);
    RegisterOne("get_leave_episode",
        "Return push+label rows for one leave episode from leave_episodes.jsonl",
        {{"t_push_ms", "Episode push timestamp; omit for latest push", "integer", false}},
        &CallGetLeaveEpisode);
    RegisterOne("get_leave_window_samples",
        "Sparse GPS/walk samples after push from leave_window_samples.jsonl",
        {{"t_push_ms", "Filter by push timestamp; omit for latest", "integer", false},
            {"limit", "Max samples (default 100, max 500)", "integer", false}},
        &CallGetLeaveWindowSamples);
    RegisterOne("get_leave_sensor_summary",
        "High-density wifi/cell/gps/mag semantics around t_push (not raw CSV rows)",
        {{"t_push_ms", "Episode center; omit → latest push", "integer", false},
            {"t_center_ms", "Alias of t_push_ms", "integer", false},
            {"before_s", "Seconds before center (default 600)", "integer", false},
            {"after_s", "Seconds after center (default 1200)", "integer", false},
            {"session_dir", "Ability dump session path; omit → latest", "string", false}},
        &CallGetLeaveSensorSummary);
    RegisterOne("get_episode_semantic_timeline",
        "Bounded aligned semantic timeline; missing values remain distinct from observed zero",
        {{"episode_id", "Exact episode identifier", "string", true},
            {"anchor_id", "Exact anchor id from get_anchors", "string", true},
            {"outcome_t_ms", "Required if id ambiguous", "integer", false},
            {"bin_s", "5..60 seconds", "integer", false}, {"start_ms", "Optional window start", "integer", false},
            {"end_ms", "Optional window end", "integer", false}, {"max_bins", "1..120", "integer", false}},
        &CallGetSemanticTimeline);
    RegisterOne("get_episode_dynamic_diagnostics",
        "Semantic event intervals, vertical recovery descriptors and cross-sensor lags",
        {{"episode_id", "Exact episode identifier", "string", true},
            {"anchor_id", "Exact anchor id from get_anchors", "string", true},
            {"outcome_t_ms", "Required if id ambiguous", "integer", false},
            {"start_ms", "Optional window start", "integer", false}, {"end_ms", "Optional window end", "integer", false}},
        &CallGetDynamicDiagnostics);

    return EvidenceToolNames();
}

}  // namespace sa_agent
