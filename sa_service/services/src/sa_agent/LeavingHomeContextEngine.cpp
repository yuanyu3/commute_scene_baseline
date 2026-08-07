/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */
#include "sa_agent/LeavingHomeContextEngine.h"

#include <mutex>

#include "AnyValue.h"
#include "ResourceManager.h"

#if defined(SA_AGENT_ENABLE_OHOS_CONTEXT_ENGINE)
#include "utils/database/OHOSRelationalDB.h"
#include "utils/database/OHOSVectorDB.h"
#endif

namespace sa_agent {
namespace {

std::once_flag gContextEngineRegistered;

jiuwen::DBConfig MakeOhosDbConfig(const LeavingHomeContextEngineOptions &options, jiuwen::DBType dbType,
    const std::string &dbName)
{
    jiuwen::DBConfig config;
    config.dbProvider = "ohos";
    config.dbType = dbType;
    config.dbName = dbName;
    config.databaseDir = options.databaseDir;
    config.optionalConf = {
        {"bundleName", jiuwen::AnyValue(options.bundleName)},
        {"moduleName", jiuwen::AnyValue(options.moduleName)},
    };
    return config;
}

jiuwen::ModelConfig MakeContextEngineModelConfig(const std::string &apiKey, const std::string &apiBase)
{
    jiuwen::ModelConfig modelConfig;
    modelConfig.apiKey = apiKey;
    modelConfig.apiBase = apiBase.empty() ? std::string(kContextEngineLlmApiBase) : apiBase;
    modelConfig.conf = {
        {"model", jiuwen::AnyValue(std::string(kContextEngineLlmModel))},
        {"stream", jiuwen::AnyValue(false)},
        {"max_tokens", jiuwen::AnyValue(2048)},
        {"temperature", jiuwen::AnyValue(0.2f)},
        {"top_k", jiuwen::AnyValue(3)},
        {"top_p", jiuwen::AnyValue(0.7f)},
        {"think", jiuwen::AnyValue(false)},
        {"enable_thinking", jiuwen::AnyValue(false)},
        {"enable_search", jiuwen::AnyValue(false)},
        {"tool_choice", jiuwen::AnyValue(std::string("auto"))},
    };
    return modelConfig;
}

jiuwen::ModelConfig MakeEmbeddingModelConfig(const std::string &apiKey)
{
    jiuwen::ModelConfig embeddingConfig;
    embeddingConfig.apiKey = apiKey;
    embeddingConfig.apiBase = kEmbeddingApiBase;
    embeddingConfig.conf = {
        {"model", jiuwen::AnyValue(std::string(kContextEngineEmbeddingModel))},
        {"stream", jiuwen::AnyValue(false)},
        {"max_tokens", jiuwen::AnyValue(2048)},
        {"temperature", jiuwen::AnyValue(0.2f)},
        {"tool_choice", jiuwen::AnyValue(std::string("auto"))},
    };
    return embeddingConfig;
}

} // namespace

void EnsureLeavingHomeContextEngineRegistered()
{
#if defined(SA_AGENT_ENABLE_OHOS_CONTEXT_ENGINE)
    std::call_once(gContextEngineRegistered, []() {
        jiuwen::ResourceManager::GetInstance().RegisterDB(
            "ohos_rdb", [](const jiuwen::DBConfig &dbConf) -> std::shared_ptr<jiuwen::BaseDB> {
                return std::make_shared<database::OHOSRelationalDB>(dbConf);
            });

        jiuwen::ResourceManager::GetInstance().RegisterDB(
            "ohos_vdb", [](const jiuwen::DBConfig &dbConf) -> std::shared_ptr<jiuwen::BaseDB> {
                return std::make_shared<database::OHOSVectorDB>(dbConf);
            });
    });
#else
    std::call_once(gContextEngineRegistered, []() {});
#endif
}

jiuwen::ContextEngineConfig BuildLeavingHomeBaselineContextEngineConfig(
    const std::string &apiKey, const LeavingHomeContextEngineOptions &options, const std::string &apiBase)
{
    EnsureLeavingHomeContextEngineRegistered();

    jiuwen::ContextEngineConfig contextEngineConfig;
    contextEngineConfig.dbConfigs[jiuwen::DBType::RDB] =
        MakeOhosDbConfig(options, jiuwen::DBType::RDB, options.rdbName);

    if (options.enableVectorDb) {
        contextEngineConfig.dbConfigs[jiuwen::DBType::VDB] =
            MakeOhosDbConfig(options, jiuwen::DBType::VDB, options.vdbName);
        contextEngineConfig.embeddingConfig = MakeEmbeddingModelConfig(apiKey);
    }

    contextEngineConfig.modelConfig = MakeContextEngineModelConfig(apiKey, apiBase);
    contextEngineConfig.maxConversationNum = options.maxConversationNum;
    contextEngineConfig.semanticThreshold = options.semanticThreshold;

    return contextEngineConfig;
}

} // namespace sa_agent
