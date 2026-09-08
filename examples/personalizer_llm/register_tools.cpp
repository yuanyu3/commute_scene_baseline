#include "register_tools.h"

#include "commute_sa/action_ops.h"
#include "commute_sa/context_template.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/personalization_optimizer.h"

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
std::string AnalyzeRules(const std::string &p) { return commute_sa::AnalyzePersonalizationRulesAction(p); }
std::string OptimizeSemanticPlan(const std::string &p)
{
    return commute_sa::RunConstrainedThetaOptimizerAction(p);
}
std::string RunRuleOptimizer(const std::string &p) { return commute_sa::RunRulePersonalizationAction(p); }
std::string GetOptimizerTrial(const std::string &p) { return commute_sa::GetOptimizationTrialAction(p); }
std::string CommitOptimizerTrial(const std::string &p) { return commute_sa::CommitOptimizedThetaAction(p); }
std::string DiscardOptimizerTrial(const std::string &p) { return commute_sa::DiscardOptimizationTrialAction(p); }
std::string GetProfile(const std::string &p) { return commute_sa::GetPersonalizationProfileAction(p); }
std::string ProposeProfile(const std::string &p) { return commute_sa::ProposeContextProfileUpdateAction(p); }
std::string SubmitAnalysis(const std::string &p) { return commute_sa::SubmitAgentAnalysisAction(p); }
std::string GetTemplateCatalog(const std::string &p) { return commute_sa::GetContextTemplateCatalogAction(p); }
std::string GetAbortedCandidates(const std::string &p)
{
    return commute_sa::GetAbortedLeaveCandidatesAction(p);
}
std::string ProposeAbortedLeave(const std::string &p)
{
    return commute_sa::ProposeAbortedLeaveInterpretationAction(p);
}
std::string GenerateTemplate(const std::string &p) { return commute_sa::GenerateContextTemplateAction(p); }
std::string GetTemplateTrial(const std::string &p) { return commute_sa::GetContextTemplateTrialAction(p); }
std::string CommitTemplate(const std::string &p) { return commute_sa::CommitContextTemplateAction(p); }
std::string DiscardTemplate(const std::string &p) { return commute_sa::DiscardContextTemplateAction(p); }
std::string GetActiveTemplate(const std::string &p) { return commute_sa::GetActiveContextTemplateAction(p); }
std::string GetPolicy(const std::string &p) { return commute_sa::GetPersonalizationPolicyAction(p); }
std::string GetPolicyCatalog(const std::string &p) { return commute_sa::GetPolicyCatalogAction(p); }
std::string BeginPolicyTrial(const std::string &p) { return commute_sa::BeginPolicyTrialAction(p); }
std::string ApplyPolicy(const std::string &p) { return commute_sa::ApplyPolicyCandidateAction(p); }
std::string EvalPolicy(const std::string &p) { return commute_sa::EvaluatePolicyOnHistoryAction(p); }
std::string RevertPolicy(const std::string &p) { return commute_sa::RevertPolicyTrialAction(p); }
std::string CommitPolicy(const std::string &p) { return commute_sa::CommitPolicyTrialAction(p); }

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
    Reg("evaluate_theta_on_history", "Replay LeaveHsmm on stored leave-window obs (can evaluate w_*)",
        {{"since_ms", "optional", "integer", false}, {"limit", "optional", "integer", false}}, &EvalHistory);
    Reg("begin_theta_trial", "Snapshot θ before try/eval loop", {}, &BeginTrial);
    Reg("revert_theta_trial", "Restore θ snapshot", {}, &RevertTrial);
    Reg("commit_theta_trial", "Keep current θ only if the C++ replay commit guard passes", {}, &CommitTrial);
    Reg("apply_theta_delta", "Apply one clipped theta delta",
        {{"param", "name", "string", true}, {"delta", "signed", "number", true},
            {"reason", "evidence reason", "string", false}},
        &ApplyDelta);
    Reg("analyze_personalization_rules",
        "Deterministic ES-CRO error-signature diagnosis and ranked semantic intervention plan",
        {{"limit", "max historical episodes", "integer", false}}, &AnalyzeRules);
    Reg("run_constrained_theta_optimizer",
        "Generate bounded numeric candidates from semantic blocks, replay HSMM history, and stage the best eligible candidate",
        {{"primary_block", "radio_reliability|motion_reliability|geo_reliability|baro_reliability|time_prior|preleave_duration|leaving_duration|trigger_threshold|arm_timing", "string", true},
            {"primary_direction", "increase|decrease", "string", true},
            {"secondary_block", "optional second semantic block", "string", false},
            {"secondary_direction", "increase|decrease", "string", false},
            {"objective", "balanced|false_push|missed_leave|lead", "string", false},
            {"max_candidates", "1..20, default 12", "integer", false},
            {"min_improvement", "hard minimum score gain", "number", false}},
        &OptimizeSemanticPlan);
    Reg("run_rule_personalization",
        "Non-Agent baseline: deterministic diagnosis plus the same constrained replay optimizer",
        {{"limit", "max historical episodes", "integer", false}}, &RunRuleOptimizer);
    Reg("get_optimization_trial", "Inspect staged candidates and hard-guard results", {}, &GetOptimizerTrial);
    Reg("commit_optimized_theta", "Commit only the deterministic best candidate that passed all C++ guards", {},
        &CommitOptimizerTrial);
    Reg("discard_optimization_trial", "Discard staged optimizer candidates without changing live theta", {},
        &DiscardOptimizerTrial);
    Reg("get_personalization_profile",
        "Return deterministic cross-episode sensor applicability plus validated context memory",
        {{"limit", "max episodes", "integer", false}}, &GetProfile);
    Reg("propose_context_profile_update",
        "Propose deterministic context memory; code accepts only with enough positive support and few contradictions",
        {{"context_name", "short context name", "string", true},
            {"context_signature", "online-computable boolean signature", "string", true},
            {"support_count", ">=3", "integer", true}, {"positive_count", ">=1", "integer", true},
            {"contradiction_count", "<=50% support", "integer", true},
            {"confidence", "0..1", "number", true}}, &ProposeProfile);
    Reg("submit_agent_analysis", "Validate and persist structured hypotheses and semantic intervention audit",
        {{"context_name", "context", "string", true}, {"primary_cause", "cause", "string", true},
            {"supporting_evidence", "evidence summary", "string", true},
            {"contradicting_evidence", "counterevidence or none", "string", true},
            {"intervention_block", "context_template for the current production path", "string", false},
            {"direction", "compose for context_template", "string", false},
            {"confidence", "0..1", "number", true}, {"abstain", "no optimization", "boolean", true}},
        &SubmitAnalysis);
    Reg("get_context_template_catalog",
        "Return bounded event/effect primitives that the Agent may compose into a new context template", {},
        &GetTemplateCatalog);
    Reg("get_aborted_leave_candidates",
        "Return read-only per-episode descent, ascent, closure, outside and support-event timing",
        {{"side", "company|home", "string", false}, {"limit", "1..100", "integer", false}},
        &GetAbortedCandidates);
    Reg("propose_aborted_leave_interpretation",
        "Propose a FALSE_PUSH as ABORTED_LEAVE; C++ requires a confirmed shared prefix, ascent, vertical closure and no outside",
        {{"side", "company|home", "string", true}, {"episode_id", "exact episode id", "string", true},
            {"confidence", "0.5..1", "number", true}, {"rationale", "evidence-grounded inference", "string", true}},
        &ProposeAbortedLeave);
    Reg("diagnose_context_template",
        "Read-only active-template ablation on the same history: compare per-episode push timing, prefix readiness, completion and gates. Not physical causal proof; never tunes or commits.",
        {{"ablation", "disable positive|negative|both sequence terms or cancel_path", "string", true},
            {"path_index", "zero-based alternative path to remove for cancel_path ablation", "integer", false},
            {"limit", "1..100 episodes, default 20", "integer", false}},
        &commute_sa::DiagnoseContextTemplateOnHistoryAction);
    Reg("generate_context_template",
        "Validate an Agent-composed event sequence, deterministically estimate requested parameter families, replay strengths, and stage the best safe template",
        {{"template_name", "new stable identifier", "string", true},
            {"side", "company|home", "string", true},
            {"anchor_id", "context anchor identifier", "string", true},
            {"applicability", "always|baro_ready", "string", true},
            {"positive_sequence", "comma-separated supported events in temporal order", "string", true},
            {"cancel_sequence", "optional ordered return events after the positive prefix starts", "string", false},
            {"cancel_paths", "optional alternatives: comma-ordered events, pipe-separated paths; see catalog; excludes cancel_sequence", "string", false},
            {"negative_pattern", "comma-separated events that jointly suppress nonspecific leave evidence", "string", false},
            {"parameter_families", "optional vertical_threshold; departure_time is disabled; C++ estimates values", "string", false},
            {"rationale", "evidence-grounded explanation", "string", true}},
        &GenerateTemplate);
    Reg("get_context_template_trial", "Inspect generated template, replay candidates, and hard-guard results", {},
        &GetTemplateTrial);
    Reg("commit_context_template", "Persist only the best generated template candidate that passed C++ guards", {},
        &CommitTemplate);
    Reg("discard_context_template", "Discard the staged template without changing the active context profile", {},
        &DiscardTemplate);
    Reg("get_active_context_template", "Return the currently persisted executable context template", {},
        &GetActiveTemplate);
    Reg("write_audit", "Append audit entry / no_op",
        {{"message", "text", "string", true}, {"changes", "object", "object", false}}, &WriteAudit);
    Reg("request_anchor_reestimate", "Queue anchor re-inference",
        {{"which", "home|company|both", "string", true}}, &ReqAnchor);
    Reg("get_personalization_policy", "Return active bounded high-level strategy", {}, &GetPolicy);
    Reg("get_policy_catalog", "Return allowed strategy templates and bounds", {}, &GetPolicyCatalog);
    Reg("begin_policy_trial", "Snapshot active policy before a strategy experiment", {}, &BeginPolicyTrial);
    Reg("apply_policy_candidate", "Apply one validated policy template candidate",
        {{"template_name", "catalog template", "string", true},
            {"probability_threshold", "optional bounded override", "number", false}}, &ApplyPolicy);
    Reg("evaluate_policy_on_history", "Counterfactual replay on semantic policy_history",
        {{"limit", "max semantic samples", "integer", false}}, &EvalPolicy);
    Reg("revert_policy_trial", "Restore policy snapshot", {}, &RevertPolicy);
    Reg("commit_policy_trial", "Activate candidate and clear trial", {}, &CommitPolicy);

    return {"get_theta", "get_anchors", "get_error_stats", "get_leave_episode", "get_leave_window_samples",
        "get_leave_sensor_summary", "get_param_limits", "evaluate_theta_on_history", "begin_theta_trial",
        "revert_theta_trial", "commit_theta_trial", "apply_theta_delta", "write_audit", "request_anchor_reestimate",
        "analyze_personalization_rules", "run_constrained_theta_optimizer", "run_rule_personalization",
        "get_optimization_trial", "commit_optimized_theta", "discard_optimization_trial",
        "get_personalization_profile", "propose_context_profile_update", "submit_agent_analysis",
        "get_context_template_catalog", "get_aborted_leave_candidates",
        "propose_aborted_leave_interpretation", "diagnose_context_template", "generate_context_template", "get_context_template_trial",
        "commit_context_template", "discard_context_template", "get_active_context_template",
        "get_personalization_policy", "get_policy_catalog", "begin_policy_trial", "apply_policy_candidate",
        "evaluate_policy_on_history", "revert_policy_trial", "commit_policy_trial"};
}

}  // namespace personalizer
