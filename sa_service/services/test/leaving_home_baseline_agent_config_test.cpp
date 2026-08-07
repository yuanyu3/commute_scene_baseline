/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * Host-side assertions for LeavingHomeBaselineAgentConfig and ContextEngine.
 *
 * Build (from repo root, MSVC) — requires JIUWEN_CORE_INCLUDE pointing at jiuwen-lite headers:
 *   cl /EHsc /std:c++17 /utf-8 /I services\include /I services\test /I %JIUWEN_CORE_INCLUDE% ^
 *     /Fe:leaving_home_baseline_agent_config_test.exe ^
 *     services\src\sa_agent\LeavingHomeBaselineAgentConfig.cpp ^
 *     services\src\sa_agent\LeavingHomeContextEngine.cpp ^
 *     services\test\stubs\AnyValueHostStub.cpp ^
 *     services\test\leaving_home_baseline_agent_config_test.cpp /link /SUBSYSTEM:CONSOLE
 */
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "stubs/HostTypeCompletions.h"
#include "sa_agent/LeavingHomeBaselineAgentConfig.h"
#include "sa_agent/LeavingHomeContextEngine.h"

namespace {

int gFailures = 0;

void ExpectTrue(bool cond, const char *msg)
{
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        ++gFailures;
    } else {
        std::cout << "PASS: " << msg << std::endl;
    }
}

std::string GetConfString(const jiuwen::AgentConfig &cfg, const std::string &key)
{
    auto it = cfg.modelConfig.conf.find(key);
    if (it == cfg.modelConfig.conf.end()) {
        return {};
    }
    if (auto s = it->second.get<std::string>()) {
        return *s;
    }
    return {};
}

float GetConfFloat(const jiuwen::AgentConfig &cfg, const std::string &key, bool *ok)
{
    *ok = false;
    auto it = cfg.modelConfig.conf.find(key);
    if (it == cfg.modelConfig.conf.end()) {
        return 0.0f;
    }
    if (auto f = it->second.get<float>()) {
        *ok = true;
        return *f;
    }
    if (auto d = it->second.get<double>()) {
        *ok = true;
        return static_cast<float>(*d);
    }
    return 0.0f;
}

bool ConfContainsKey(const jiuwen::AgentConfig &cfg, const std::string &key)
{
    return cfg.modelConfig.conf.find(key) != cfg.modelConfig.conf.end();
}

std::string GetCtxConfString(const jiuwen::ContextEngineConfig &ctx, const std::string &key)
{
    auto it = ctx.modelConfig.conf.find(key);
    if (it == ctx.modelConfig.conf.end()) {
        return {};
    }
    if (auto s = it->second.get<std::string>()) {
        return *s;
    }
    return {};
}

} // namespace

int main()
{
    const std::string fakeBaseUrl = "https://test.example.com/v1/chat/completions";
    const std::string fakeApiKey = "test-api-key-not-a-secret";

    sa_agent::LeavingHomeContextEngineOptions ctxOptions;
    ctxOptions.bundleName = "com.example.sa";
    ctxOptions.moduleName = "entry";
    ctxOptions.databaseDir = "/data/storage/el2/database/entry";
    ctxOptions.rdbName = "test_sa_context.db";
    ctxOptions.maxConversationNum = 42;
    ctxOptions.semanticThreshold = 1.0f;

    auto ctx = sa_agent::BuildLeavingHomeBaselineContextEngineConfig(fakeApiKey, ctxOptions);
    ExpectTrue(ctx.dbConfigs.count(jiuwen::DBType::RDB) == 1, "ContextEngine RDB configured");
    ExpectTrue(ctx.dbConfigs.at(jiuwen::DBType::RDB).dbName == "test_sa_context.db", "ContextEngine RDB name");
    ExpectTrue(ctx.dbConfigs.at(jiuwen::DBType::RDB).dbProvider == "ohos", "ContextEngine RDB provider");
    ExpectTrue(GetCtxConfString(ctx, "model") == sa_agent::kContextEngineLlmModel,
               "ContextEngine internal LLM model");
    ExpectTrue(std::string(sa_agent::kContextEngineLlmModel) == "qwen3-8b",
               "ContextEngine LLM is qwen3-8b");
    ExpectTrue(ctx.modelConfig.apiBase == sa_agent::kContextEngineLlmApiBase,
               "ContextEngine LLM apiBase defaults to DashScope");
    ExpectTrue(ctx.modelConfig.apiBase ==
                   "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
               "ContextEngine LLM apiBase URL");

    auto ctxWithBase = sa_agent::BuildLeavingHomeBaselineContextEngineConfig(
        fakeApiKey, ctxOptions, fakeBaseUrl);
    ExpectTrue(ctxWithBase.modelConfig.apiBase == fakeBaseUrl,
               "ContextEngine LLM apiBase can be injected");

    ExpectTrue(ctx.embeddingConfig.apiBase.empty(), "ContextEngine embedding skipped when VDB off");
    ExpectTrue(ctx.maxConversationNum == 42, "ContextEngine maxConversationNum");
    ExpectTrue(std::fabs(ctx.semanticThreshold - 1.0f) < 1e-6f, "ContextEngine semanticThreshold default");
    ExpectTrue(ctx.dbConfigs.count(jiuwen::DBType::VDB) == 0, "ContextEngine VDB disabled by default");

    auto cfg = sa_agent::BuildLeavingHomeBaselineAgentConfig(fakeBaseUrl, fakeApiKey, ctxOptions);
    ExpectTrue(cfg != nullptr, "BuildLeavingHomeBaselineAgentConfig returns non-null");
    ExpectTrue(cfg->contextEngineConfig.modelConfig.apiBase == fakeBaseUrl,
               "Agent ContextEngine uses same baseUrl as ReAct model");

    ExpectTrue(sa_agent::BuildLeavingHomeBaselineAgentConfig("", fakeApiKey, ctxOptions) == nullptr,
               "empty baseUrl returns null");
    ExpectTrue(sa_agent::BuildLeavingHomeBaselineAgentConfig(fakeBaseUrl, "", ctxOptions) == nullptr,
               "empty apiKey returns null");

    ExpectTrue(cfg->mode == jiuwen::AgentType::REACT, "mode is REACT");
    ExpectTrue(cfg->maxTurn == 1, "maxTurn is 1");
    ExpectTrue(cfg->useToolSelector == false, "useToolSelector is false (empty tools)");
    ExpectTrue(cfg->id == sa_agent::kAgentId, "agent id matches");
    ExpectTrue(cfg->name == sa_agent::kAgentName, "agent name matches");
    ExpectTrue(cfg->version == sa_agent::kPromptVersion, "version is prompt version");
    ExpectTrue(std::string(sa_agent::GetLeavingHomeBaselinePromptVersion()) == "leaving_home_baseline_prompt_v1",
               "prompt version getter");

    ExpectTrue(GetConfString(*cfg, "model") == sa_agent::kModelName, "model is qwen3-8b");
    ExpectTrue(std::string(sa_agent::kModelName) == "qwen3-8b", "kModelName is qwen3-8b");
    ExpectTrue(cfg->modelConfig.apiBase == fakeBaseUrl, "apiBase comes from caller injection");
    ExpectTrue(cfg->modelConfig.apiKey == fakeApiKey, "apiKey comes from caller injection");

    bool tempOk = false;
    float temperature = GetConfFloat(*cfg, "temperature", &tempOk);
    ExpectTrue(tempOk && std::fabs(temperature) < 1e-6f, "temperature is 0");

    ExpectTrue(cfg->switchConfig.reflection == false, "reflection disabled");
    ExpectTrue(cfg->switchConfig.summary == false, "summary disabled");
    ExpectTrue(cfg->switchConfig.addPrompt == false, "addPrompt disabled");

    ExpectTrue(cfg->contextEngineConfig.maxConversationNum == 42, "contextEngineConfig attached");
    ExpectTrue(cfg->contextEngineConfig.dbConfigs.count(jiuwen::DBType::RDB) == 1,
               "agent config carries ContextEngine RDB");
    ExpectTrue(std::fabs(cfg->contextEngineConfig.semanticThreshold - 1.0f) < 1e-6f,
               "contextEngineConfig semanticThreshold preserved");

    const auto &prompt = cfg->promptTemplates["system"];
    ExpectTrue(!prompt.empty(), "system prompt loaded");
    ExpectTrue(prompt.find("perception_tick") != std::string::npos, "prompt mentions perception_tick");
    ExpectTrue(prompt.find("observation_window") != std::string::npos, "prompt mentions observation_window");
    ExpectTrue(prompt.find("motion.transition") != std::string::npos, "prompt mentions motion.transition");
    ExpectTrue(prompt.find("pdr.transition") != std::string::npos, "prompt mentions pdr.transition");
    ExpectTrue(prompt.find("pdr.window") != std::string::npos, "prompt mentions pdr.window");
    ExpectTrue(prompt.find("pdr.cumulative") != std::string::npos, "prompt mentions pdr.cumulative");
    ExpectTrue(prompt.find("fact_id") != std::string::npos, "prompt mentions fact_id");
    ExpectTrue(prompt.find("AT_HOME") != std::string::npos, "prompt mentions AT_HOME");
    ExpectTrue(prompt.find("LEAVING_HOME") != std::string::npos, "prompt mentions LEAVING_HOME");
    ExpectTrue(prompt.find("AWAY_FROM_HOME") != std::string::npos, "prompt mentions AWAY_FROM_HOME");
    ExpectTrue(prompt.find("UNKNOWN") != std::string::npos, "prompt mentions UNKNOWN");
    ExpectTrue(prompt.find("DEPARTURE_NOTIFICATION") != std::string::npos,
               "prompt mentions DEPARTURE_NOTIFICATION");
    ExpectTrue(prompt.find("只能输出一个 JSON 对象") != std::string::npos, "prompt requires JSON-only output");
    ExpectTrue(prompt.find("不要输出思维链") != std::string::npos, "prompt forbids chain of thought");
    ExpectTrue(prompt.find("Thought") != std::string::npos && prompt.find("Action") != std::string::npos &&
                   prompt.find("Observation") != std::string::npos,
               "prompt forbids Thought/Action/Observation");
    ExpectTrue(prompt.find("scene_decision") != std::string::npos, "prompt mentions scene_decision");
    ExpectTrue(prompt.find("time_context") != std::string::npos &&
                   prompt.find("不得假定存在该字段") != std::string::npos,
               "prompt rejects time_context requirement");

    ExpectTrue(prompt.find("recent_snapshots") == std::string::npos, "no explicit recent_snapshots window");
    ExpectTrue(!ConfContainsKey(*cfg, "recent_snapshots"), "conf has no recent_snapshots");
    ExpectTrue(!ConfContainsKey(*cfg, "time_context"), "conf has no time_context");
    ExpectTrue(!ConfContainsKey(*cfg, "validator"), "conf has no validator");
    ExpectTrue(!ConfContainsKey(*cfg, "repair"), "conf has no repair");
    ExpectTrue(!ConfContainsKey(*cfg, "dedupe"), "conf has no dedupe");
    ExpectTrue(cfg->promptTemplates.find("thought_prompt") == cfg->promptTemplates.end(),
               "no thought_prompt (avoid ReAct thought/action JSON conflict)");

    ExpectTrue(sa_agent::GetLeavingHomeBaselineSystemPrompt() == prompt, "prompt getter matches config");

    if (gFailures > 0) {
        std::cerr << gFailures << " assertion(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "All leaving-home baseline agent config tests passed." << std::endl;
    return EXIT_SUCCESS;
}
