/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */
#ifndef SA_AGENT_LEAVING_HOME_BASELINE_AGENT_CONFIG_H
#define SA_AGENT_LEAVING_HOME_BASELINE_AGENT_CONFIG_H

#include <memory>
#include <string>

#include "AgentStruct.h"
#include "sa_agent/LeavingHomeContextEngine.h"

namespace sa_agent {

constexpr const char *kPromptVersion = "leaving_home_baseline_prompt_v1";
constexpr const char *kAgentId = "leaving-home-baseline-agent";
constexpr const char *kAgentName = "LeavingHomeBaselineAgent";
constexpr const char *kAgentDescription =
    "Context-aware proactive service baseline agent for leaving-home scene recognition.";

// model choice : [deepseek-v4-flash, qwen3-8b]
constexpr const char *kModelName = "qwen3-8b";

/**
 * Fixed system prompt for the leaving-home baseline ReActAgent.
 * Prompt version: leaving_home_baseline_prompt_v1
 */
const std::string &GetLeavingHomeBaselineSystemPrompt();

/** Returns leaving_home_baseline_prompt_v1 */
const char *GetLeavingHomeBaselinePromptVersion();

/**
 * Build AgentConfig with ContextEngine initialized by sa_agent (recommended).
 *
 * - model: qwen3-8b
 * - temperature: 0
 * - maxTurn: 1
 * - no tools (caller must not call Agent::AddTools)
 * - baseUrl and apiKey injected by caller (never hardcoded)
 * - ContextEngine built via LeavingHomeContextEngineOptions
 *
 * Returns nullptr when baseUrl or apiKey is empty.
 */
std::shared_ptr<jiuwen::AgentConfig> BuildLeavingHomeBaselineAgentConfig(
    const std::string &baseUrl, const std::string &apiKey,
    const LeavingHomeContextEngineOptions &contextOptions = {});

/**
 * Build AgentConfig with a caller-provided ContextEngineConfig (advanced override).
 * Returns nullptr when baseUrl or apiKey is empty.
 */
std::shared_ptr<jiuwen::AgentConfig> BuildLeavingHomeBaselineAgentConfig(
    const std::string &baseUrl, const std::string &apiKey,
    const jiuwen::ContextEngineConfig &contextEngineConfig);

} // namespace sa_agent

#endif // SA_AGENT_LEAVING_HOME_BASELINE_AGENT_CONFIG_H
