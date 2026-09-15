#include "register_tools.h"

#include "commute_sa/action_ops.h"
#include "commute_sa/context_template.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/evidence_strength_profile.h"
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
std::string GetSemanticTimeline(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetEpisodeSemanticTimelineJson(p);
}
std::string GetDynamicDiagnostics(const std::string &p)
{
    return commute_sa::EvidenceQuery::GetInstance().GetEpisodeDynamicDiagnosticsJson(p);
}
std::string ReqAnchor(const std::string &p)
{
    return commute_sa::RequestAnchorReestimateAction(p);
}
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
std::string GetActiveTemplate(const std::string &p) { return commute_sa::GetActiveContextTemplateAction(p); }
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
    Reg("get_episode_semantic_timeline",
        "Bounded 5-60 second aligned semantic timeline; distinguishes missing from observed zero",
        {{"episode_id", "exact episode identifier", "string", true},
            {"anchor_id", "anchor id from anchors.json", "string", true}, {"outcome_t_ms", "required if id ambiguous", "integer", false},
            {"bin_s", "5..60 seconds, default 10", "integer", false},
            {"start_ms", "optional inclusive window start", "integer", false},
            {"end_ms", "optional inclusive window end", "integer", false},
            {"max_bins", "1..120, default 120", "integer", false}}, &GetSemanticTimeline);
    Reg("get_episode_dynamic_diagnostics",
        "Exact semantic event intervals, vertical recovery descriptors and cross-sensor lags for one episode",
        {{"episode_id", "exact episode identifier", "string", true},
            {"anchor_id", "anchor id from anchors.json", "string", true}, {"outcome_t_ms", "required if id ambiguous", "integer", false},
            {"start_ms", "optional inclusive window start", "integer", false},
            {"end_ms", "optional inclusive window end", "integer", false}}, &GetDynamicDiagnostics);
    Reg("submit_agent_analysis", "Persist the final typed Agent intervention decision for auditable ablation",
        {{"family", "required with unified tools; STRUCTURE|PRIMITIVE_PARAMETER|EVIDENCE_STRENGTH|DURATION", "string", false},
            {"intervention_type", "STRUCTURE|EVIDENCE_STRENGTH|DURATION|NO_OP", "string", true},
            {"anchor_id", "exact anchor id", "string", true},
            {"decision", "COMMITTED|REJECTED|DISCARDED|NO_OP", "string", true},
            {"context_name", "context", "string", true}, {"primary_cause", "cause", "string", true},
            {"supporting_evidence", "evidence summary", "string", true},
            {"contradicting_evidence", "counterevidence; write none when absent", "string", true},
            {"missing_evidence", "missing evidence; write none when complete", "string", true},
            {"confidence", "0..1", "number", true},
            {"decision_reason", "why the final decision follows from evidence and replay", "string", true},
            {"structure_summary", "required for STRUCTURE", "string", false},
            {"target_families", "required for EVIDENCE_STRENGTH", "string", false},
            {"target_state", "PRE_LEAVE|LEAVING; required for DURATION", "string", false},
            {"tool_name", "final decisive tool; required except NO_OP", "string", false},
            {"tool_result", "compact eligible/commit/rejection result; required except NO_OP", "string", false},
            {"replay_result", "compact replay result; required except NO_OP", "string", false}},
        &SubmitAnalysis);
    Reg("get_context_template_catalog",
        "Return bounded event/effect primitives that the Agent may compose into a new context template", {},
        &GetTemplateCatalog);
    Reg("get_aborted_leave_candidates",
        "Return read-only per-episode descent, ascent, closure, outside and support-event timing",
        {{"anchor_id", "exact anchor id", "string", true}, {"limit", "1..100", "integer", false}},
        &GetAbortedCandidates);
    Reg("propose_aborted_leave_interpretation",
        "Propose a FALSE_PUSH as ABORTED_LEAVE; C++ requires a confirmed shared prefix, ascent, vertical closure and no outside",
        {{"anchor_id", "exact anchor id", "string", true}, {"episode_id", "exact episode id", "string", true},
            {"confidence", "0.5..1", "number", true}, {"rationale", "evidence-grounded inference", "string", true}},
        &ProposeAbortedLeave);
    Reg("diagnose_context_template",
        "Read-only active-template ablation on the same history: compare per-episode push timing, prefix readiness, completion and gates. Not physical causal proof; never tunes or commits.",
        {{"ablation", "disable positive|negative|both sequence terms or cancel_path", "string", true},
            {"path_index", "zero-based alternative path to remove for cancel_path ablation", "integer", false},
            {"limit", "1..100 episodes, default 20", "integer", false}},
        &commute_sa::DiagnoseContextTemplateOnHistoryAction);
    Reg("evaluate_negative_pattern_candidates", "Read-only batch counterfactual replay; no staging or commit. Compare promising negative conjunctions before rejecting them. Active strength frozen, or LOW=0.2 without a template.",
        {{"anchor_id", "exact anchor id", "string", true},
         {"candidates", "1..8 pipe-separated patterns, each 1..6 comma-separated catalog events", "string", true}},
        &commute_sa::EvaluateNegativePatternCandidatesAction);
    Reg("propose_personalization",
        "Validate an Agent-composed event sequence, deterministically estimate requested parameter families, replay strengths, and stage the best safe template",
        {{"family", "STRUCTURE|PRIMITIVE_PARAMETER|EVIDENCE_STRENGTH|DURATION", "string", true},
            {"families", "evidence channels for EVIDENCE_STRENGTH", "string", false},
            {"state", "PRE_LEAVE|LEAVING for DURATION", "string", false},
            {"template_name", "new stable identifier", "string", false},
            {"anchor_id", "context anchor identifier", "string", true},
            {"applicability", "always|baro_ready", "string", false},
            {"positive_sequence", "comma-separated supported events in temporal order", "string", false},
            {"readiness_policy", "support_only|disambiguate; latter suppresses an observed incomplete calibrated prefix", "string", false},
            {"cancel_sequence", "optional ordered return events after the positive prefix starts", "string", false},
            {"cancel_paths", "optional alternatives: comma-ordered events, pipe-separated paths; see catalog; excludes cancel_sequence", "string", false},
            {"negative_pattern", "comma-separated events that jointly suppress nonspecific leave evidence", "string", false},
            {"parameter_families", "optional vertical_threshold; departure_time is disabled; C++ estimates values", "string", false},
            {"rationale", "evidence-grounded explanation", "string", false}},
        &commute_sa::ProposePersonalizationAction);
    Reg("get_personalization_trial", "Inspect current family trial",
        {{"family", "same family as proposal", "string", true}}, &commute_sa::GetPersonalizationTrialAction);
    Reg("commit_personalization", "Commit replay-approved current family",
        {{"family", "same family as proposal", "string", true}}, &commute_sa::CommitPersonalizationAction);
    Reg("discard_personalization", "Discard current family trial",
        {{"family", "same family as proposal", "string", true}}, &commute_sa::DiscardPersonalizationAction);
    Reg("get_active_context_template", "Return the currently persisted executable context template", {},
        &GetActiveTemplate);
    Reg("get_current_user_anchor_profile", "Return committed anchor-specific profile or global fallback",
        {{"anchor_id", "exact anchor id", "string", true}},
        &commute_sa::GetUserAnchorProfileAction);
    Reg("get_personalization_memory", "Read anchor research memory; untrusted claims require fresh verification",
        {{"anchor_id", "exact anchor id", "string", true},
         {"query", "optional literal substring in claim or applicability", "string", false},
         {"offset", "pagination offset, default zero", "number", false}}, &commute_sa::GetPersonalizationMemoryAction);
    Reg("propose_memory_update", "Append evidence-linked memory revision; does not change model",
        {{"anchor_id", "exact anchor id", "string", true},
         {"memory_id", "stable ASCII id, reuse for revisions", "string", true},
         {"expected_revision", "zero for new, otherwise revision from memory read", "number", true},
         {"claim", "bounded research claim, literal UTF-8", "string", true},
         {"status", "hypothesis|supported|contested|retired", "string", true},
         {"applicability", "conditions under which claim might hold", "string", true},
         {"limitations", "counterevidence, uncertainties and version applicability", "string", true},
         {"support_episode_ids", "comma-separated existing episode ids", "string", false},
         {"counterexample_episode_ids", "comma-separated existing episode ids", "string", false},
         {"audit_id", "existing same-anchor audit; required for supported", "string", false}},
        &commute_sa::ProposeMemoryUpdateAction);
    Reg("get_personalization_history_summary",
        "Return outcome, primitive and duration facts grouped only by anchor id; no recommendation",
        {{"anchor_id", "exact anchor id", "string", true}},
        &commute_sa::GetPersonalizationHistorySummaryAction);
    Reg("request_anchor_reestimate", "Queue anchor re-inference",
        {{"which", "home|company|both", "string", true}}, &ReqAnchor);

    return {"get_theta", "get_anchors", "get_error_stats", "get_leave_episode", "get_leave_window_samples",
        "get_leave_sensor_summary", "get_episode_semantic_timeline", "get_episode_dynamic_diagnostics",
        "request_anchor_reestimate", "submit_agent_analysis",
        "get_context_template_catalog", "get_aborted_leave_candidates",
        "propose_aborted_leave_interpretation", "diagnose_context_template", "evaluate_negative_pattern_candidates", "propose_personalization", "get_personalization_trial",
        "commit_personalization", "discard_personalization", "get_active_context_template",
        "get_current_user_anchor_profile", "get_personalization_history_summary",
        "get_personalization_memory", "propose_memory_update",
        };
}

}  // namespace personalizer
