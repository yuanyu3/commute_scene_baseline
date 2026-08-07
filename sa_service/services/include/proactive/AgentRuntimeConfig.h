/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Minimal dotenv parser for SA Agent runtime credentials.
 */
#ifndef AGENT_RUNTIME_CONFIG_H
#define AGENT_RUNTIME_CONFIG_H

#include <string>

namespace OHOS::Multimedia::CameraAgentService {

constexpr const char *kAgentEnvKeyBaseUrl = "SA_AGENT_BASE_URL";
constexpr const char *kAgentEnvKeyApiKey = "SA_AGENT_API_KEY";
/** Optional verbose helloworld-style dumps (sensor_events/ticks/snapshots). Default OFF. */
constexpr const char *kAgentEnvKeyDebugSinks = "SA_AGENT_DEBUG_SINKS";

#ifdef PROACTIVE_AGENT_HOST_TEST
constexpr const char *kAgentEnvFilePath = "agent.env";
#else
constexpr const char *kAgentEnvFilePath = "/data/service/el2/9903/agent.env";
#endif

/**
 * Credentials + optional debug dumps.
 * Product minimal set is always on (leave_episodes / param_changes / theta / anchors).
 */
struct AgentRuntimeEnvironment {
    std::string baseUrl;
    std::string apiKey;
    bool debugSinks = false;
};

struct AgentEnvLoadResult {
    bool ok = false;
    std::string error;
    AgentRuntimeEnvironment environment;
};

AgentEnvLoadResult LoadAgentEnvironmentFromFile(const std::string &path);
AgentRuntimeEnvironment LoadAgentDebugSinkDefaultsFromFile(const std::string &path);

} // namespace OHOS::Multimedia::CameraAgentService

#endif // AGENT_RUNTIME_CONFIG_H
