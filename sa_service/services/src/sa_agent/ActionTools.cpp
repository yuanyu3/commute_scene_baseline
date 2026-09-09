/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Jiuwen action tools for θ personalizer.
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

std::string CallAnalyzeRules(const std::string &p) { return commute_sa::AnalyzePersonalizationRulesAction(p); }
std::string CallConstrainedOptimizer(const std::string &p)
{
    return commute_sa::RunConstrainedThetaOptimizerAction(p);
}
std::string CallRulePersonalization(const std::string &p)
{
    return commute_sa::RunRulePersonalizationAction(p);
}
std::string CallGetOptimizationTrial(const std::string &p)
{
    return commute_sa::GetOptimizationTrialAction(p);
}
std::string CallCommitOptimizedTheta(const std::string &p)
{
    return commute_sa::CommitOptimizedThetaAction(p);
}
std::string CallDiscardOptimizationTrial(const std::string &p)
{
    return commute_sa::DiscardOptimizationTrialAction(p);
}
std::string CallGetPersonalizationProfile(const std::string &p)
{
    return commute_sa::GetPersonalizationProfileAction(p);
}
std::string CallProposeContextProfile(const std::string &p)
{
    return commute_sa::ProposeContextProfileUpdateAction(p);
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

std::string CallGetPolicy(const std::string &p) { return commute_sa::GetPersonalizationPolicyAction(p); }
std::string CallGetPolicyCatalog(const std::string &p) { return commute_sa::GetPolicyCatalogAction(p); }
std::string CallBeginPolicyTrial(const std::string &p) { return commute_sa::BeginPolicyTrialAction(p); }
std::string CallApplyPolicy(const std::string &p) { return commute_sa::ApplyPolicyCandidateAction(p); }
std::string CallEvaluatePolicy(const std::string &p) { return commute_sa::EvaluatePolicyOnHistoryAction(p); }
std::string CallRevertPolicy(const std::string &p) { return commute_sa::RevertPolicyTrialAction(p); }
std::string CallCommitPolicy(const std::string &p) { return commute_sa::CommitPolicyTrialAction(p); }

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
        "write_audit",
        "request_anchor_reestimate",
        "get_param_limits",
        "evaluate_theta_on_history",
        "analyze_personalization_rules",
        "get_personalization_profile",
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

    RegisterOne("apply_theta_delta",
        "Apply one clipped theta delta and persist (param_changes + theta.json)",
        {{"param", "enter_leave|exit_leave|w_walk|w_pdr|w_geo|w_wifi|w_cell|w_ble|w_radio|w_time|w_baro|"
                   "weekday_leave_home_hour|weekday_leave_company_hour|arm_delay_s|hsmm_preleave_min_s|"
                   "hsmm_preleave_mean_s|hsmm_preleave_max_s|hsmm_leaving_min_s|hsmm_leaving_mean_s|"
                   "hsmm_leaving_max_s|lead_min_s|lead_max_s|baro_min_descent_m",
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

    RegisterOne("get_param_limits", "Return code-enforced min/max/step for writable params", {}, &CallGetParamLimits);

    RegisterOne("evaluate_theta_on_history",
        "Score current θ by replaying LeaveHsmm on stored leave-window observations (can evaluate w_*). Higher score is better.",
        {{"since_ms", "optional epoch ms lower bound", "integer", false},
            {"limit", "max episodes, default 30", "integer", false}},
        &CallEvaluateThetaOnHistory);

    RegisterOne("begin_theta_trial",
        "Snapshot θ before try→evaluate→revert/commit loop", {}, &CallBeginThetaTrial);

    RegisterOne("revert_theta_trial", "Restore θ to begin_theta_trial snapshot", {}, &CallRevertThetaTrial);

    RegisterOne("commit_theta_trial", "Keep current theta only if the C++ replay guard passes", {}, &CallCommitThetaTrial);

    RegisterOne("analyze_personalization_rules",
        "Deterministic ES-CRO diagnosis and ranked semantic intervention plan",
        {{"limit", "max historical episodes", "integer", false}}, &CallAnalyzeRules);
    RegisterOne("run_constrained_theta_optimizer",
        "Generate bounded candidates from semantic blocks and stage the best replay-safe candidate",
        {{"primary_block", "semantic parameter block", "string", true},
            {"primary_direction", "increase|decrease", "string", true},
            {"secondary_block", "optional semantic block", "string", false},
            {"secondary_direction", "increase|decrease", "string", false},
            {"objective", "balanced|false_push|missed_leave|lead", "string", false},
            {"max_candidates", "1..20", "integer", false},
            {"min_improvement", "minimum score gain; code floor is 0.25", "number", false}},
        &CallConstrainedOptimizer);
    RegisterOne("run_rule_personalization", "Non-Agent rule diagnosis plus the same replay optimizer",
        {{"limit", "max historical episodes", "integer", false}}, &CallRulePersonalization);
    RegisterOne("get_optimization_trial", "Inspect staged candidates and hard-guard results", {},
        &CallGetOptimizationTrial);
    RegisterOne("commit_optimized_theta", "Commit only the deterministic best hard-guarded candidate", {},
        &CallCommitOptimizedTheta);
    RegisterOne("discard_optimization_trial", "Discard staged candidates without changing live theta", {},
        &CallDiscardOptimizationTrial);
    RegisterOne("get_personalization_profile",
        "Return deterministic sensor applicability and validated cross-episode context memory",
        {{"limit", "max historical episodes", "integer", false}}, &CallGetPersonalizationProfile);
    RegisterOne("propose_context_profile_update",
        "Persist context memory only when support/conflict gates pass",
        {{"context_name", "short name", "string", true},
            {"context_signature", "online-computable signature", "string", true},
            {"support_count", ">=3", "integer", true}, {"positive_count", ">=1", "integer", true},
            {"contradiction_count", "<=50% support", "integer", true},
            {"confidence", "0..1", "number", true}}, &CallProposeContextProfile);
    RegisterOne("submit_agent_analysis", "Validate and persist structured Agent hypotheses",
        {{"context_name", "context", "string", true}, {"primary_cause", "cause", "string", true},
            {"supporting_evidence", "evidence summary", "string", true},
            {"contradicting_evidence", "counterevidence or none", "string", true},
            {"intervention_block", "semantic block", "string", false},
            {"direction", "increase|decrease", "string", false},
            {"confidence", "0..1", "number", true}, {"abstain", "no optimization", "boolean", true}},
        &CallSubmitAgentAnalysis);

    RegisterOne("get_context_template_catalog", "Return bounded context-template primitives", {},
        &CallGetContextTemplateCatalog);
    RegisterOne("get_aborted_leave_candidates", "Return read-only physical reversal evidence per episode",
        {{"side", "company|home", "string", false}, {"limit", "1..100", "integer", false}},
        &CallGetAbortedLeaveCandidates);
    RegisterOne("propose_aborted_leave_interpretation",
        "Validate a proposed FALSE_PUSH as ABORTED_LEAVE from physical reversal evidence",
        {{"side", "company|home", "string", true}, {"episode_id", "exact episode id", "string", true},
            {"confidence", "0.5..1", "number", true}, {"rationale", "evidence-grounded inference", "string", true}},
        &CallProposeAbortedLeave);
    RegisterOne("diagnose_context_template", "Read-only active-template sequence ablation",
        {{"ablation", "positive|negative|both|cancel_path", "string", true},
            {"path_index", "zero-based alternative path for cancel_path ablation", "integer", false},
            {"limit", "1..100 episodes", "integer", false}}, &CallDiagnoseContextTemplate);
    RegisterOne("generate_context_template", "Stage a replay-safe Agent-composed context template",
        {{"template_name", "stable identifier", "string", true}, {"side", "company|home", "string", true},
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
    RegisterOne("get_current_user_anchor_profile", "Return committed anchor-specific evidence strengths",
        {{"side", "company|home", "string", true}, {"anchor_id", "exact anchor id", "string", true}},
        &CallGetUserAnchorProfile);
    RegisterOne("estimate_evidence_strength", "Fit selected strengths and stage a replay-checked candidate",
        {{"side", "company|home", "string", true}, {"anchor_id", "exact anchor id", "string", true},
            {"families", "comma-separated evidence channels", "string", false}},
        &CallEstimateEvidenceStrength);
    RegisterOne("get_evidence_strength_trial", "Inspect staged strength candidate", {},
        &CallGetEvidenceStrengthTrial);
    RegisterOne("commit_evidence_strength_candidate", "Commit replay-safe anchor profile", {},
        &CallCommitEvidenceStrength);
    RegisterOne("discard_evidence_strength_candidate", "Discard staged strength candidate", {},
        &CallDiscardEvidenceStrength);
    RegisterOne("fit_duration_prior", "Fit PRE_LEAVE or LEAVING duration and stage a replay-checked candidate",
        {{"side", "company|home", "string", true}, {"anchor_id", "exact anchor id", "string", true},
            {"state", "PRE_LEAVE|LEAVING", "string", true}}, &CallFitDurationPrior);
    RegisterOne("get_duration_prior_trial", "Inspect staged duration candidate", {}, &CallGetDurationPriorTrial);
    RegisterOne("commit_duration_prior_candidate", "Commit replay-safe anchor duration", {},
        &CallCommitDurationPrior);
    RegisterOne("discard_duration_prior_candidate", "Discard staged duration candidate", {},
        &CallDiscardDurationPrior);

    RegisterOne("get_personalization_policy", "Return active bounded high-level strategy", {}, &CallGetPolicy);
    RegisterOne("get_policy_catalog", "Return allowed strategy templates and bounds", {}, &CallGetPolicyCatalog);
    RegisterOne("begin_policy_trial", "Snapshot active strategy before counterfactual experiment", {}, &CallBeginPolicyTrial);
    RegisterOne("apply_policy_candidate", "Apply a validated strategy template candidate",
        {{"template_name", "confirmed_leaving", "string", true},
            {"probability_threshold", "optional bounded override", "number", false}}, &CallApplyPolicy);
    RegisterOne("evaluate_policy_on_history", "Counterfactual replay against semantic policy_history.jsonl",
        {{"limit", "max semantic samples", "integer", false}}, &CallEvaluatePolicy);
    RegisterOne("revert_policy_trial", "Restore policy snapshot", {}, &CallRevertPolicy);
    RegisterOne("commit_policy_trial", "Keep active candidate policy", {}, &CallCommitPolicy);

    return ActionToolNames();
}

}  // namespace sa_agent
