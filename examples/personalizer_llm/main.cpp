#include "register_tools.h"

#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"

#include "Agent.h"
#include "AnyValue.h"
#include "ErrorCode.h"
#include "ResourceManager.h"
#include "os_adapters/include/log/log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using jiuwen::Agent;
using jiuwen::AgentConfig;
using jiuwen::AgentType;
using jiuwen::ErrorCode;
using jiuwen::FormatType;
using jiuwen::Request;
using jiuwen::StreamData;
using jiuwen::StreamFilter;
using jiuwen::StreamMode;

namespace {

std::string Trim(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

bool LoadDotEnv(const std::string &path, std::string *baseUrl, std::string *apiKey, std::string *model)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            // Strip UTF-8 BOM if present.
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
                line = line.substr(3);
            }
            first = false;
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string k = Trim(line.substr(0, eq));
        std::string v = Trim(line.substr(eq + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
            v = v.substr(1, v.size() - 2);
        }
        if (k == "SA_AGENT_BASE_URL" || k == "JIUWEN_API_BASE") {
            *baseUrl = v;
        } else if (k == "SA_AGENT_API_KEY" || k == "JIUWEN_API_KEY") {
            *apiKey = v;
        } else if (k == "SA_AGENT_MODEL" || k == "JIUWEN_MODEL") {
            *model = v;
        }
    }
    return !baseUrl->empty() && !apiKey->empty();
}

std::string NormalizeApiBase(std::string base)
{
    if (base == "https://api.deepseek.com" || base == "https://api.deepseek.com/") {
        return "https://api.deepseek.com/chat/completions";
    }
    // DeepSeek OpenAI-compat often accepts /v1/chat/completions; keep if already full.
    if (base.find("/chat/completions") == std::string::npos) {
        if (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        base += "/chat/completions";
    }
    return base;
}

std::string WithBearer(std::string key)
{
    if (key.rfind("Bearer ", 0) != 0) {
        key = "Bearer " + key;
    }
    return key;
}

std::string ReadFile(const std::string &path)
{
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void WriteFile(const std::string &path, const std::string &body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

bool MkDir(const std::string &p)
{
    struct stat st {};
    if (stat(p.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(p.c_str(), 0755) == 0;
}

std::string PrepareFixture(const std::string &root)
{
    MkDir(root);
    const std::string session = root + "/20260806_session";
    MkDir(session);
    WriteFile(root + "/theta.json",
        "{\n  \"enter_leave\": 0.58,\n  \"exit_leave\": 0.45,\n  \"min_evidence\": 2,\n"
        "  \"w_walk\": 0.25,\n  \"w_pdr\": 0.2,\n  \"w_geo\": 0.2,\n"
        "  \"w_wifi\": 0.12,\n  \"w_cell\": 0.08,\n  \"w_ble\": 0.02,\n"
        "  \"w_radio\": 0.22,\n  \"w_time\": 0.2,\n  \"w_baro\": 0.2,\n"
        "  \"arm_delay_s\": 25,\n"
        "  \"weekday_leave_home_hour\": 8.25\n}\n");
    WriteFile(root + "/anchors.json",
        "{\n  \"coordinate_system\": \"WGS84\",\n"
        "  \"home\": {\"id\":\"home\",\"lat\":40.05,\"lon\":116.17,\"r_in_m\":50,\"r_out_m\":90,\"method\":\"seed\"},\n"
        "  \"company\": {\"id\":\"co\",\"lat\":40.0,\"lon\":116.3,\"r_in_m\":80,\"r_out_m\":120,\"method\":\"seed\"}\n}\n");
    WriteFile(root + "/leave_episodes.jsonl",
        "{\"type\":\"push\",\"t_push_ms\":1700000000000,\"intent\":\"LEAVE_COMPANY_NOTIFICATION\","
        "\"scene\":\"LEAVING_COMPANY\",\"score_home\":0.1,\"score_company\":0.72,\"dist_home_m\":55,\"walking\":true}\n"
        "{\"type\":\"label\",\"t_label_ms\":1700001200000,\"t_push_ms\":1700000000000,"
        "\"label\":\"FALSE_PUSH\",\"side\":\"company\",\"home_relation\":\"INSIDE\",\"dist_home_m\":12}\n");
    WriteFile(root + "/leave_window_samples.jsonl",
        "{\"t_ms\":1700000060000,\"t_push_ms\":1700000000000,\"lat\":40.0501,\"lon\":116.1702,"
        "\"acc\":15,\"walking\":true,\"home_relation\":\"NEAR\",\"dist_home_m\":48}\n");
    WriteFile(root + "/policy_history.jsonl",
        "{\"t_ms\":1700000000000,\"label\":\"FALSE_PUSH\",\"preleave_probability\":0.55,"
        "\"leaving_probability\":0.60,\"hits\":2,\"walking\":true,\"wifi_detach\":false,"
        "\"cell_leave\":true,\"ble_detach\":false,\"pdr_net_out_m\":2,\"geo_outbound\":false,"
        "\"has_usable_gps\":false,\"evidence_duration_s\":6}\n"
        "{\"t_ms\":1700001000000,\"label\":\"CONFIRMED_LEAVE\",\"preleave_probability\":0.67,"
        "\"leaving_probability\":0.30,\"hits\":3,\"walking\":true,\"wifi_detach\":true,"
        "\"cell_leave\":true,\"ble_detach\":false,\"pdr_net_out_m\":4,\"geo_outbound\":false,"
        "\"has_usable_gps\":false,\"evidence_duration_s\":8,\"lead_s\":43}\n");
    WriteFile(session + "/wifi_data_smoke.csv",
        "wallTsMs,bssid,ssid,rssi,freq,power_mode\n"
        "1699999700000,aa:bb:cc:dd:ee:01,HomeWiFi,-45,2412,HIGH_STILL\n"
        "1700000005000,aa:bb:cc:dd:ee:01,HomeWiFi,-48,2412,HIGH_WALKING\n");
    WriteFile(session + "/cell_data_smoke.csv",
        "wallTsMs,type,cellId,signalIntensity,mcc,mnc,pci,tac,earfcn,power_mode\n"
        "1700000000000,LTE,12345,-90,460,00,10,100,1850,HIGH_WALKING\n");
    WriteFile(session + "/mag_data_smoke.csv",
        "wallTsMs,x,y,z,power_mode\n"
        "1700000000000,12.1,-3.2,41.0,HIGH_WALKING\n");
    WriteFile(session + "/location_data_smoke.csv",
        "wallTsMs,lat,lon,acc,power_mode\n"
        "1700000000000,40.0502,116.1703,20,HIGH_WALKING\n"
        "1700000300000,40.05005,116.17015,14,HIGH_WALKING\n");
    return root;
}

bool PathExistsFile(const std::string &p)
{
    struct stat st {};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

const char *kSystemPrompt = R"(你是离开锚点检测的低频个性化研究 Agent。实时推断由端侧 HSMM 完成。
比较完整 episode 与不同结果，区分传感器不可用和可用但无变化，提出带支持、反证、缺失证据和适用条件的可证伪假设。
先调用 get_anchors，并逐字使用 focus_side 对应的真实 anchor_id；方向判断必须针对该锚点。
只能用 get_context_template_catalog 返回的原语组合模板。positive_sequence 有时间顺序；negative_pattern 的事件必须同时成立。
已有模板时可用 diagnose_context_template 关闭正向、负向或全部序列证据，比较逐episode推送和门控来检验假设；这不是物理因果证明，无收益或缺少真值可no-op。
不得修改 theta、提供数值强度、生成代码或绕过产品门控。最多生成一次模板；C++ 全历史回放没有安全候选时必须 discard/no-op。
单个确认样本必须标记高泛化风险。最后提交结构化归因并写入审计。)";

const char *kSystemPromptFixture = kSystemPrompt;

std::string BuildQueryFromEpisodes(const std::string &dataRoot)
{
    const std::string body = ReadFile(dataRoot + "/leave_episodes.jsonl");
    int64_t tPush = 0;
    std::string intent = "DEPARTURE_NOTIFICATION";
    std::string scene = "LEAVING_HOME";
    std::string label;
    std::string reason = "AFTER_PUSH";
    std::istringstream iss(body);
    std::string line;
    std::string lastPush;
    std::string lastLabel;
    auto isType = [](const std::string &s, const char *ty) -> bool {
        // accept "type":"push" or "type": "push"
        const std::string a = std::string("\"type\":\"") + ty + "\"";
        const std::string b = std::string("\"type\": \"") + ty + "\"";
        return s.find(a) != std::string::npos || s.find(b) != std::string::npos;
    };
    std::string lastCompanyPush;
    std::string lastCompanyLabel;
    while (std::getline(iss, line)) {
        if (isType(line, "push")) {
            lastPush = line;
            if (line.find("LEAVING_COMPANY") != std::string::npos ||
                line.find("LEAVE_COMPANY") != std::string::npos) {
                lastCompanyPush = line;
            }
        }
        if (isType(line, "label")) {
            lastLabel = line;
            if (line.find("\"side\": \"company\"") != std::string::npos ||
                line.find("\"side\":\"company\"") != std::string::npos) {
                lastCompanyLabel = line;
            }
        }
    }
    if (!lastCompanyPush.empty()) {
        lastPush = lastCompanyPush;
    }
    if (!lastCompanyLabel.empty()) {
        lastLabel = lastCompanyLabel;
    }
    auto grabInt = [](const std::string &s, const char *key) -> int64_t {
        const std::string pat = std::string("\"") + key + "\":";
        auto pos = s.find(pat);
        if (pos == std::string::npos) {
            return 0;
        }
        pos += pat.size();
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) {
            ++pos;
        }
        return std::strtoll(s.c_str() + pos, nullptr, 10);
    };
    auto grabStr = [](const std::string &s, const char *key) -> std::string {
        const std::string pat = std::string("\"") + key + "\":";
        auto pos = s.find(pat);
        if (pos == std::string::npos) {
            return {};
        }
        pos += pat.size();
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) {
            ++pos;
        }
        if (pos >= s.size() || s[pos] != '"') {
            return {};
        }
        ++pos;
        std::string out;
        for (size_t i = pos; i < s.size() && s[i] != '"'; ++i) {
            out.push_back(s[i]);
        }
        return out;
    };
    // The job is the authoritative closed-episode trigger.  Unlike the old
    // push-only path it can represent MISSED_LEAVE and explicit TRUE_NEGATIVE.
    const std::string jobsBody = ReadFile(dataRoot + "/personalize_jobs.jsonl");
    std::istringstream jobsStream(jobsBody);
    std::string jobLine;
    std::string lastJob;
    while (std::getline(jobsStream, jobLine)) {
        if (!jobLine.empty()) {
            lastJob = jobLine;
        }
    }
    if (!lastPush.empty()) {
        tPush = grabInt(lastPush, "t_push_ms");
        const std::string i = grabStr(lastPush, "intent");
        const std::string sc = grabStr(lastPush, "scene");
        if (!i.empty()) {
            intent = i;
        }
        if (!sc.empty()) {
            scene = sc;
        }
    }
    if (!lastLabel.empty()) {
        label = grabStr(lastLabel, "label");
    }
    if (!lastJob.empty()) {
        const std::string jobReason = grabStr(lastJob, "reason");
        const std::string jobLabel = grabStr(lastJob, "label");
        const std::string jobIntent = grabStr(lastJob, "intent");
        const std::string jobScene = grabStr(lastJob, "scene");
        if (!jobReason.empty()) reason = jobReason;
        if (!jobLabel.empty()) label = jobLabel;
        if (!jobIntent.empty()) intent = jobIntent;
        if (!jobScene.empty()) scene = jobScene;
        tPush = grabInt(lastJob, "t_push_ms");
    }
    std::ostringstream q;
    q << "{\"task\":\"personalize_leave_strategy\",\"reason\":\"" << reason
      << "\",\"focus_side\":\"company\",\"last_intent\":\""
      << intent << "\",\"last_scene\":\"" << scene << "\",\"last_push_at_ms\":" << tPush << ",\"label\":\""
      << label
      << "\",\"instruction\":\"focus_side=company. Actively inspect evidence, compare the deterministic rule "
          "diagnosis and form falsifiable context hypotheses. Compose a new template only from "
          "get_context_template_catalog primitives. You may request supported parameter_families when evidence "
          "shows that family is relevant, but never provide numeric values, strengths, or modify theta. C++ estimates "
          "values from history and may refuse when support is insufficient. The query label "
          "is authoritative and RETURN_TO_COMPANY must never be treated as MISSED_LEAVE. Commit only when the C++ "
          "template replay exposes an eligible best candidate; otherwise discard and audit no_op.\"}";
    return q.str();
}

/** Extract JSON string field "key":"..." (best-effort). */
std::string JsonStrField(const std::string &json, const char *key)
{
    const std::string pat = std::string("\"") + key + "\":\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) {
        return {};
    }
    pos += pat.size();
    std::string out;
    for (size_t i = pos; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            out.push_back(json[++i]);
            continue;
        }
        if (c == '"') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

void PrettyPrintStream(const std::string &payload, std::ofstream *traceFile)
{
    if (traceFile && traceFile->is_open()) {
        (*traceFile) << payload << "\n";
        traceFile->flush();
    }

    const std::string type = JsonStrField(payload, "type");
    const std::string sub = JsonStrField(payload, "subType");
    if (type == "assistant") {
        // Prefer data.text / status final
        const std::string text = JsonStrField(payload, "text");
        if (!text.empty()) {
            std::cout << "\n----- ASSISTANT -----\n" << text << "\n";
            return;
        }
    }
    if (type == "tool") {
        const std::string name = JsonStrField(payload, "name");
        const std::string status = JsonStrField(payload, "status");
        if (status == "start") {
            const std::string args = JsonStrField(payload, "args");
            std::cout << "\n>>> TOOL START  " << name << "\n    args=" << (args.empty() ? "{}" : args) << "\n";
            return;
        }
        if (status == "end") {
            const std::string err = JsonStrField(payload, "errorMsg");
            // result may be unescaped in "result":"..."
            std::string result = JsonStrField(payload, "result");
            if (result.empty() && payload.find("\"isError\":true") != std::string::npos) {
                std::cout << "<<< TOOL END    " << name << "  ERROR\n    " << err << "\n";
            } else {
                if (result.size() > 800) {
                    result = result.substr(0, 800) + "...(truncated)";
                }
                std::cout << "<<< TOOL END    " << name << "\n    result=" << result << "\n";
            }
            return;
        }
    }
    if (type == "agent") {
        if (sub == "turn_start") {
            std::cout << "\n===== TURN START =====\n";
            return;
        }
        if (sub == "turn_end") {
            std::cout << "===== TURN END =====\n";
            return;
        }
        const std::string msg = JsonStrField(payload, "message");
        if (!msg.empty()) {
            std::cout << "[agent] " << sub << " " << msg << "\n";
            return;
        }
    }
    // fallback raw (short)
    if (payload.size() > 300) {
        std::cout << "[stream] " << payload.substr(0, 300) << "...\n";
    } else {
        std::cout << "[stream] " << payload << "\n";
    }
}

bool EnvFlagTrue(const char *name)
{
    const char *v = std::getenv(name);
    if (!v) {
        return false;
    }
    return std::string(v) == "1" || std::string(v) == "true" || std::string(v) == "TRUE" || std::string(v) == "on";
}

}  // namespace

int main(int argc, char **argv)
{
    bool debug = EnvFlagTrue("PERSONALIZER_DEBUG") || EnvFlagTrue("SA_AGENT_DEBUG");
    bool noFixture = EnvFlagTrue("PERSONALIZER_NO_FIXTURE");
    bool diagnosticOnly = EnvFlagTrue("PERSONALIZER_DIAGNOSTIC_ONLY");
    bool noTools = EnvFlagTrue("PERSONALIZER_NO_TOOLS");
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug" || std::string(argv[i]) == "-d") {
            debug = true;
        }
        if (std::string(argv[i]) == "--no-fixture") {
            noFixture = true;
        }
        if (std::string(argv[i]) == "--diagnostic-only") {
            diagnosticOnly = true;
        }
        if (std::string(argv[i]) == "--no-tools") {
            noTools = true;
        }
    }

    // The current Jiuwen SDK exposes LOG(DEBUG) but no global SetMinLogLevel API.
    // Keep debug selection local to the host's trace/console output.
    (void)jiuwen::ResourceManager::GetInstance();

    std::string envPath = "sa_service/etc/agent.env";
    std::string dataRoot = "examples/personalizer_llm/run_data";
    // positional: [env] [data]  (ignore flags with optional values)
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--debug" || a == "-d" || a == "--no-fixture" || a == "--diagnostic-only" ||
            a == "--no-tools") {
            continue;
        }
        if (a == "--max-turn" || a == "--system-prompt" || a == "--query-file") {
            if (i + 1 < argc) {
                ++i;  // skip value
            }
            continue;
        }
        pos.push_back(a);
    }
    if (pos.size() >= 1) {
        envPath = pos[0];
    }
    if (pos.size() >= 2) {
        dataRoot = pos[1];
    }

    std::string baseUrl;
    std::string apiKey;
    std::string model = "deepseek-chat";
    if (!LoadDotEnv(envPath, &baseUrl, &apiKey, &model)) {
        if (const char *b = std::getenv("JIUWEN_API_BASE")) {
            baseUrl = b;
        }
        if (const char *k = std::getenv("JIUWEN_API_KEY")) {
            apiKey = k;
        }
    }
    // Credentials normally come from the dotenv file, but a one-off host run
    // still needs to select a model allowed by the current gateway key without
    // rewriting that credential file.
    if (const char *m = std::getenv("JIUWEN_MODEL")) {
        model = m;
    }
    if (baseUrl.empty() || apiKey.empty()) {
        std::cerr << "Missing API credentials. Pass agent.env path or set JIUWEN_API_BASE/KEY\n";
        return 1;
    }
    if (model.empty()) {
        model = "deepseek-chat";
    }

    if (noFixture) {
        if (!PathExistsFile(dataRoot + "/leave_episodes.jsonl") || !PathExistsFile(dataRoot + "/theta.json")) {
            std::cerr << "--no-fixture requires existing leave_episodes.jsonl and theta.json under " << dataRoot
                      << "\n";
            return 1;
        }
        std::cout << "Using existing product data (no fixture rewrite): " << dataRoot << "\n";
    } else {
        dataRoot = PrepareFixture(dataRoot);
    }
    if (!commute_sa::ProductStore::GetInstance().Init(dataRoot)) {
        std::cerr << "ProductStore init failed: " << dataRoot << "\n";
        return 1;
    }
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(dataRoot);

    auto tools = personalizer::RegisterPersonalizerTools();
    if (diagnosticOnly) {
        const std::unordered_set<std::string> allowed = {"get_theta", "get_anchors", "get_error_stats",
            "get_leave_episode", "get_leave_window_samples", "get_leave_sensor_summary", "get_param_limits",
            "evaluate_theta_on_history", "analyze_personalization_rules", "get_personalization_profile",
            "submit_agent_analysis", "write_audit", "get_active_context_template", "diagnose_context_template"};
        tools.erase(std::remove_if(tools.begin(), tools.end(),
                        [&](const std::string &name) { return allowed.count(name) == 0; }),
            tools.end());
        std::cout << "diagnostic-only tool gate enabled\n";
    } else {
        // Normal demo flow exposes Agent-composed context templates. Numeric
        // theta optimizers and legacy direct mutation tools are hidden so the
        // boundary is enforced by the host rather than only by the prompt.
        const std::unordered_set<std::string> forbidden = {"begin_theta_trial", "revert_theta_trial",
            "commit_theta_trial", "apply_theta_delta", "begin_policy_trial", "apply_policy_candidate",
            "revert_policy_trial", "commit_policy_trial", "run_constrained_theta_optimizer",
            "commit_optimized_theta", "discard_optimization_trial", "run_rule_personalization",
            "propose_context_profile_update"};
        tools.erase(std::remove_if(tools.begin(), tools.end(),
                        [&](const std::string &name) { return forbidden.count(name) != 0; }),
            tools.end());
        std::cout << "constrained-context-template tool gate enabled\n";
    }
    if (noTools) {
        tools.clear();
        std::cout << "no-tools blind-test gate enabled\n";
    }
    std::cout << "Registered tools n=" << tools.size() << " debug=" << (debug ? "ON" : "OFF") << "\n";
    std::cout << "API base=" << NormalizeApiBase(baseUrl) << " model=" << model << " data=" << dataRoot << "\n";
    std::cout << "API key configured: yes (len=" << apiKey.size() << ")\n";
    if (debug) {
        std::cout << "DEBUG: jiuwen log=DEBUG, stream pretty-print + " << dataRoot << "/agent_trace.jsonl\n";
    }

    auto cfg = std::make_shared<AgentConfig>();
    cfg->id = "commute-context-template-personalizer-host";
    cfg->name = "ContextTemplatePersonalizer";
    cfg->description = "Host Agent that composes bounded sensor-sequence templates for HSMM replay";
    cfg->version = "1.0.0";
    cfg->mode = AgentType::REACT;
    // Evidence + trial/eval/apply easily exceeds 16 ReAct turns; 16 caused TaskStatus::FAILED
    // with the misleading "maximum number of retries" tip even after write_audit succeeded.
    // Default 80; override with PERSONALIZER_MAX_TURN (or --max-turn N).
    cfg->maxTurn = 80;
    if (const char *mt = std::getenv("PERSONALIZER_MAX_TURN")) {
        try {
            const int parsed = std::stoi(mt);
            if (parsed > 0) {
                cfg->maxTurn = parsed;
            }
        } catch (...) {
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--max-turn" && i + 1 < argc) {
            try {
                const int parsed = std::stoi(argv[i + 1]);
                if (parsed > 0) {
                    cfg->maxTurn = parsed;
                }
            } catch (...) {
            }
        }
    }
    std::cout << "maxTurn=" << cfg->maxTurn << "\n";
    {
        // Prefer PERSONALIZER_SYSTEM_PROMPT / --system-prompt; else project default.
        std::string promptPath = "jiuwen_agent/system_prompt.md";
        if (const char *envPrompt = std::getenv("PERSONALIZER_SYSTEM_PROMPT")) {
            if (envPrompt[0] != '\0') {
                promptPath = envPrompt;
            }
        }
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--system-prompt" && i + 1 < argc) {
                promptPath = argv[i + 1];
            }
        }
        std::string loaded = ReadFile(promptPath);
        if (!loaded.empty()) {
            cfg->promptTemplates["system"] = loaded;
            std::cout << "system prompt ← " << promptPath << " (" << loaded.size() << " bytes)\n";
        } else {
            std::cerr << "WARN: prompt not found: " << promptPath << "; using embedded fallback\n";
            cfg->promptTemplates["system"] = noFixture ? kSystemPrompt : kSystemPromptFixture;
        }
    }
    cfg->modelConfig.apiKey = WithBearer(apiKey);
    cfg->modelConfig.apiBase = NormalizeApiBase(baseUrl);
    cfg->modelConfig.formatType = FormatType::OPENAI;
    // This SDK build registers its OpenAI-compatible implementation as "qwen";
    // the endpoint and model below still select the configured DeepSeek service.
    cfg->modelConfig.conf["model_provider"] = jiuwen::AnyValue(std::string("qwen"));
    cfg->modelConfig.conf["model"] = jiuwen::AnyValue(model);
    cfg->modelConfig.conf["stream"] = jiuwen::AnyValue(false);
    cfg->modelConfig.conf["temperature"] = jiuwen::AnyValue(0.1f);
    // Gateway Qwen/DeepSeek-V4 emit reasoning_content by default; disable to cut latency.
    cfg->modelConfig.conf["enable_thinking"] = jiuwen::AnyValue(false);
    // Current Jiuwen initializes ContextEngine from this nested config. Keep it
    // identical to the agent chat model so intent/memory initialization sees it.
    cfg->contextEngineConfig.modelConfig = cfg->modelConfig;
    cfg->switchConfig.reflection = false;
    cfg->switchConfig.summary = false;
    cfg->switchConfig.addPrompt = false;

    std::shared_ptr<Agent> agent;
    try {
        agent = std::make_shared<Agent>(cfg);
    } catch (const std::exception &ex) {
        std::cerr << "Agent ctor failed: " << ex.what() << "\n";
        return 1;
    }
    if (!tools.empty() && agent->AddTools(tools) != ErrorCode::SUCCESS) {
        std::cerr << "AddTools failed\n";
        return 1;
    }

    auto req = std::make_shared<Request>();
    req->sessionId = "personalize-host-1";
    req->requestId = "personalize-req-1";
    std::string queryPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--query-file" && i + 1 < argc) {
            queryPath = argv[i + 1];
        }
    }
    if (!queryPath.empty()) {
        req->query = ReadFile(queryPath);
        if (req->query.empty()) {
            std::cerr << "Query file is empty or unreadable: " << queryPath << "\n";
            return 1;
        }
        std::cout << "query <- " << queryPath << " (" << req->query.size() << " bytes)\n";
    } else {
        req->query = BuildQueryFromEpisodes(dataRoot);
    }
    std::cout << "query=" << req->query << "\n";
    // Always dump stream payloads to agent_trace.jsonl (so you can open it after any run).
    // DEBUG adds: TRACE mode, jiuwen DEBUG logs, prettier turn/tool banners.
    StreamFilter filter{debug ? StreamMode::TRACE : StreamMode::OUTPUT, {}};
    const std::string tracePath = dataRoot + "/agent_trace.jsonl";
    std::ofstream traceFile(tracePath, std::ios::out | std::ios::trunc);
    if (!traceFile) {
        std::cerr << "WARN: cannot open trace file: " << tracePath << "\n";
    }

    std::cout << "\n=== Invoke personalizer LLM" << (debug ? " (DEBUG)" : "") << " ===\n";
    std::cout << "trace file: " << tracePath << "\n";
    if (!debug) {
        std::cout << "(tip: PERSONALIZER_DEBUG=1 or --debug for richer console + TRACE stream)\n";
    }

    int streamEvents = 0;
    const auto resp = agent->Invoke(req, filter, [&](const std::shared_ptr<StreamData> &data) {
        if (!data || data->payload.empty()) {
            return;
        }
        ++streamEvents;
        PrettyPrintStream(data->payload, traceFile.is_open() ? &traceFile : nullptr);
    });

    // Always append final Invoke message — stream callbacks may be empty depending on jiuwen mode.
    if (traceFile.is_open()) {
        traceFile << "{\"type\":\"final_response\",\"stream_events\":" << streamEvents
                  << ",\"status\":" << static_cast<int>(resp.status)
                  << ",\"errorCode\":" << static_cast<int>(resp.errorCode)
                  << ",\"message\":" << (resp.message.empty() ? "null" : resp.message) << "}\n";
        traceFile.flush();
        traceFile.close();
    }

    std::cout << "\nstatus=" << static_cast<int>(resp.status) << " errorCode=" << static_cast<int>(resp.errorCode)
              << " stream_events=" << streamEvents << "\n";
    if (debug || streamEvents == 0) {
        std::cout << "raw message=" << resp.message << "\n";
    }

    std::cout << "\n=== theta.json after ===\n" << ReadFile(dataRoot + "/theta.json") << "\n";
    std::cout << "=== param_changes.jsonl ===\n" << ReadFile(dataRoot + "/param_changes.jsonl") << "\n";
    std::cout << "=== audit.jsonl ===\n" << ReadFile(dataRoot + "/audit.jsonl") << "\n";
    std::cout << "=== agent_trace.jsonl ===\n" << tracePath
              << " (stream_events=" << streamEvents << "; final_response always appended)\n";

    const bool toolsFinished = resp.message.find("\"name\":\"write_audit\"") != std::string::npos;
    // Jiuwen maps maxTurn exhaustion to TaskStatus::FAILED with a "retries exceeded" tip,
    // even when every tool succeeded. Match the device SA: errorCode is the real failure bit.
    if (resp.errorCode != ErrorCode::SUCCESS) {
        return 2;
    }
    if (resp.status == jiuwen::TaskStatus::FAILED && !toolsFinished) {
        return 2;
    }
    if (resp.status == jiuwen::TaskStatus::FAILED && toolsFinished) {
        std::cout << "WARN: Jiuwen TaskStatus=FAILED after write_audit (usually maxTurn); treating as success\n";
    }
    return 0;
}
