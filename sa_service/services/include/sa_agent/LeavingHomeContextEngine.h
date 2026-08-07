/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */
#ifndef SA_AGENT_LEAVING_HOME_CONTEXT_ENGINE_H
#define SA_AGENT_LEAVING_HOME_CONTEXT_ENGINE_H

#include <string>

#include "AgentStruct.h"

namespace sa_agent {

/** ContextEngine internal LLM used for session/history management (not the main ReActAgent model). */
constexpr const char* kContextEngineLlmModel = "qwen3-8b";
constexpr const char* kContextEngineLlmApiBase =
    "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
constexpr const char* kContextEngineEmbeddingModel = "text-embedding-v4";
constexpr const char* kEmbeddingApiBase =
    "https://dashscope.aliyuncs.com/compatible-mode/v1/embeddings";

/**
 * Host-app paths for OHOS ContextEngine persistence.
 * Override bundleName / databaseDir when porting to the target SA HAP.
 */
struct LeavingHomeContextEngineOptions {
    std::string bundleName = "com.example.AssistantAgent";
    std::string moduleName = "entry";
    std::string databaseDir = "/data/storage/el2/database/entry";
    std::string rdbName = "sa_leaving_home_context.db";
    std::string vdbName = "sa_leaving_home_context_vector.db";
    /** Enable VDB config (API 20+). RDB is always configured for conversation history. */
    bool enableVectorDb = false;
    size_t maxConversationNum = 100;
    /** Default 1.0f: no semantic history retrieval for leaving-home baseline. */
    float semanticThreshold = 1.0f;
};

/**
 * Register OHOS RDB/VDB creators with ResourceManager (idempotent).
 * Active when SA_AGENT_ENABLE_OHOS_CONTEXT_ENGINE is defined (vendored OHOS*DB sources).
 * Embedding model is not registered for the leaving-home baseline (VDB off by default).
 */
void EnsureLeavingHomeContextEngineRegistered();

/**
 * Build ContextEngineConfig for leaving-home baseline (RDB session history + internal CE models).
 * Calls EnsureLeavingHomeContextEngineRegistered() internally.
 * apiBase: LLM endpoint; empty falls back to kContextEngineLlmApiBase (DashScope / qwen).
 */
jiuwen::ContextEngineConfig BuildLeavingHomeBaselineContextEngineConfig(
    const std::string& apiKey, const LeavingHomeContextEngineOptions& options = {},
    const std::string& apiBase = {});

}  // namespace sa_agent

#endif  // SA_AGENT_LEAVING_HOME_CONTEXT_ENGINE_H
