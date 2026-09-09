/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Jiuwen tools for constrained departure personalization.
 */
#include "sa_agent/ActionTools.h"

#include "commute_sa/action_ops.h"
#include "commute_sa/context_template.h"
#include "commute_sa/evidence_strength_profile.h"
#include "commute_sa/personalization_optimizer.h"

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

std::string CallRequestAnchorReestimate(const std::string &params)
{
    return commute_sa::RequestAnchorReestimateAction(params);
}
std::string CallSubmitAgentAnalysis(const std::string &p)
{
    return commute_sa::SubmitAgentAnalysisAction(p);
}
std::string CallGetContextTemplateCatalog(const std::string &p)
{
    return commute_sa::GetContextTemplateCatalogAction(p);
}
std::string CallGetAbortedLeaveCandidates(const std::string &p)
{
    return commute_sa::GetAbortedLeaveCandidatesAction(p);
}
std::string CallProposeAbortedLeave(const std::string &p)
{
    return commute_sa::ProposeAbortedLeaveInterpretationAction(p);
}
std::string CallDiagnoseContextTemplate(const std::string &p)
{
    return commute_sa::DiagnoseContextTemplateOnHistoryAction(p);
}
std::string CallGenerateContextTemplate(const std::string &p)
{
    return commute_sa::GenerateContextTemplateAction(p);
}
std::string CallGetContextTemplateTrial(const std::string &p)
{
    return commute_sa::GetContextTemplateTrialAction(p);
}
std::string CallCommitContextTemplate(const std::string &p)
{
    return commute_sa::CommitContextTemplateAction(p);
}
std::string CallDiscardContextTemplate(const std::string &p)
{
    return commute_sa::DiscardContextTemplateAction(p);
}
std::string CallGetActiveContextTemplate(const std::string &p)
{
    return commute_sa::GetActiveContextTemplateAction(p);
}
std::string CallGetUserAnchorProfile(const std::string &p)
{
    return commute_sa::GetUserAnchorProfileAction(p);
}
std::string CallGetPersonalizationHistorySummary(const std::string &p)
{
    return commute_sa::GetPersonalizationHistorySummaryAction(p);
}
std::string CallEstimateEvidenceStrength(const std::string &p)
{
    return commute_sa::EstimateEvidenceStrengthAction(p);
}
std::string CallGetEvidenceStrengthTrial(const std::string &p)
{
    return commute_sa::GetEvidenceStrengthTrialAction(p);
}
std::string CallCommitEvidenceStrength(const std::string &p)
{
    return commute_sa::CommitEvidenceStrengthCandidateAction(p);
}
std::string CallDiscardEvidenceStrength(const std::string &p)
{
    return commute_sa::DiscardEvidenceStrengthCandidateAction(p);
}
std::string CallFitDurationPrior(const std::string &p) { return commute_sa::FitDurationPriorAction(p); }
std::string CallGetDurationPriorTrial(const std::string &p) { return commute_sa::GetDurationPriorTrialAction(p); }
std::string CallCommitDurationPrior(const std::string &p) { return commute_sa::CommitDurationPriorCandidateAction(p); }
std::string CallDiscardDurationPrior(const std::string &p) { return commute_sa::DiscardDurationPriorCandidateAction(p); }

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
        "request_anchor_reestimate",
        "submit_agent_analysis",
        "get_context_template_catalog",
        "get_aborted_leave_candidates",
        "propose_aborted_leave_interpretation",
        "diagnose_context_template",
        "generate_context_template",
        "get_context_template_trial",
        "commit_context_template",
        "discard_context_template",
        "get_active_context_template",
        "get_current_user_anchor_profile",
        "get_personalization_history_summary",
        "estimate_evidence_strength",
        "get_evidence_strength_trial",
        "commit_evidence_strength_candidate",
        "discard_evidence_strength_candidate",
        "fit_duration_prior",
        "get_duration_prior_trial",
        "commit_duration_prior_candidate",
        "discard_duration_prior_candidate",
    };
    return kNames;
}

std::vector<std::string> RegisterActionTools()
{
    (void)ResourceManager::GetInstance();

    RegisterOne("request_anchor_reestimate",
        "Queue offline home/company anchor re-inference job (anchor_reestimate_jobs.jsonl)",
        {{"which", "home|company|both", "string", true}}, &CallRequestAnchorReestimate);

    RegisterOne("submit_agent_analysis", "Persist the final typed Agent intervention decision for audit and ablation",
        {{"intervention_type", "STRUCTURE|EVIDENCE_STRENGTH|DURATION|NO_OP", "string", true},
            {"anchor_id", "exact anchor id", "string", true},
            {"decision", "COMMITTED|REJECTED|DISCARDED|NO_OP", "string", true},
            {"context_name", "context", "string", true}, {"primary_cause", "cause", "string", true},
            {"supporting_evidence", "evidence summary", "string", true},
            {"contradicting_evidence", "counterevidence; none if absent", "string", true},
            {"missing_evidence", "missing evidence; none if complete", "string", true},
            {"confidence", "0..1", "number", true},
            {"decision_reason", "evidence/replay-grounded final reason", "string", true},
            {"structure_summary", "required for STRUCTURE", "string", false},
            {"target_families", "required for EVIDENCE_STRENGTH", "string", false},
            {"target_state", "PRE_LEAVE|LEAVING; required for DURATION", "string", false},
            {"tool_name", "final decisive tool; required except NO_OP", "string", false},
            {"tool_result", "compact tool result; required except NO_OP", "string", false},
            {"replay_result", "compact replay result; required except NO_OP", "string", false}},
        &CallSubmitAgentAnalysis);

    RegisterOne("get_context_template_catalog", "Return bounded context-template primitives", {},
        &CallGetContextTemplateCatalog);
    RegisterOne("get_aborted_leave_candidates", "Return read-only physical reversal evidence per episode",
        {{"anchor_id", "exact anchor id", "string", true}, {"limit", "1..100", "integer", false}},
        &CallGetAbortedLeaveCandidates);
    RegisterOne("propose_aborted_leave_interpretation",
        "Validate a proposed FALSE_PUSH as ABORTED_LEAVE from physical reversal evidence",
        {{"anchor_id", "exact anchor id", "string", true}, {"episode_id", "exact episode id", "string", true},
            {"confidence", "0.5..1", "number", true}, {"rationale", "evidence-grounded inference", "string", true}},
        &CallProposeAbortedLeave);
    RegisterOne("diagnose_context_template", "Read-only active-template sequence ablation",
        {{"ablation", "positive|negative|both|cancel_path", "string", true},
            {"path_index", "zero-based alternative path for cancel_path ablation", "integer", false},
            {"limit", "1..100 episodes", "integer", false}}, &CallDiagnoseContextTemplate);
    RegisterOne("generate_context_template", "Stage a replay-safe Agent-composed context template",
        {{"template_name", "stable identifier", "string", true},
            {"anchor_id", "exact anchor id", "string", true},
            {"applicability", "always|baro_ready", "string", true},
            {"positive_sequence", "ordered catalog events", "string", true},
            {"cancel_sequence", "optional ordered return events", "string", false},
            {"cancel_paths", "optional comma-ordered, pipe-separated return paths; see catalog; excludes cancel_sequence", "string", false},
            {"negative_pattern", "optional same-tick conjunction", "string", false},
            {"parameter_families", "optional vertical_threshold", "string", false},
            {"rationale", "evidence-grounded explanation", "string", true}}, &CallGenerateContextTemplate);
    RegisterOne("get_context_template_trial", "Inspect staged template candidates", {},
        &CallGetContextTemplateTrial);
    RegisterOne("commit_context_template", "Commit only the C++-selected safe candidate", {},
        &CallCommitContextTemplate);
    RegisterOne("discard_context_template", "Discard the staged context template", {},
        &CallDiscardContextTemplate);
    RegisterOne("get_active_context_template", "Return the persisted executable template", {},
        &CallGetActiveContextTemplate);
    RegisterOne("get_current_user_anchor_profile", "Return committed anchor-specific profile",
        {{"anchor_id", "exact anchor id", "string", true}},
        &CallGetUserAnchorProfile);
    RegisterOne("get_personalization_history_summary", "Return facts grouped only by anchor id",
        {{"anchor_id", "exact anchor id", "string", true}}, &CallGetPersonalizationHistorySummary);
    RegisterOne("estimate_evidence_strength", "Fit selected strengths and stage a replay-checked candidate",
        {{"anchor_id", "exact anchor id", "string", true},
            {"families", "comma-separated evidence channels", "string", false}},
        &CallEstimateEvidenceStrength);
    RegisterOne("get_evidence_strength_trial", "Inspect staged strength candidate", {},
        &CallGetEvidenceStrengthTrial);
    RegisterOne("commit_evidence_strength_candidate", "Commit replay-safe anchor profile", {},
        &CallCommitEvidenceStrength);
    RegisterOne("discard_evidence_strength_candidate", "Discard staged strength candidate", {},
        &CallDiscardEvidenceStrength);
    RegisterOne("fit_duration_prior", "Fit PRE_LEAVE or LEAVING duration and stage a replay-checked candidate",
        {{"anchor_id", "exact anchor id", "string", true},
            {"state", "PRE_LEAVE|LEAVING", "string", true}}, &CallFitDurationPrior);
    RegisterOne("get_duration_prior_trial", "Inspect staged duration candidate", {}, &CallGetDurationPriorTrial);
    RegisterOne("commit_duration_prior_candidate", "Commit replay-safe anchor duration", {},
        &CallCommitDurationPrior);
    RegisterOne("discard_duration_prior_candidate", "Discard staged duration candidate", {},
        &CallDiscardDurationPrior);

    return ActionToolNames();
}

}  // namespace sa_agent
