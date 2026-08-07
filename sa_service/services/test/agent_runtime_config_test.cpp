/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Host-side tests for AgentRuntimeConfig dotenv parser.
 */
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "AgentRuntimeConfig.h"

using OHOS::Multimedia::CameraAgentService::AgentEnvLoadResult;
using OHOS::Multimedia::CameraAgentService::AgentRuntimeEnvironment;
using OHOS::Multimedia::CameraAgentService::LoadAgentDebugSinkDefaultsFromFile;
using OHOS::Multimedia::CameraAgentService::LoadAgentEnvironmentFromFile;

namespace {

void WriteTempEnv(const std::string &path, const std::string &content)
{
    std::ofstream out(path, std::ios::trunc);
    assert(out.is_open());
    out << content;
}

void RemoveTemp(const std::string &path)
{
    std::remove(path.c_str());
}

} // namespace

int main()
{
    const std::string path = "agent_runtime_config_test.env";

    WriteTempEnv(path,
        "# comment line\n"
        "\n"
        "SA_AGENT_BASE_URL = https://example.com/v1/chat/completions\n"
        "SA_AGENT_API_KEY=test-key-not-a-secret\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(r.ok);
        assert(r.environment.baseUrl == "https://example.com/v1/chat/completions");
        assert(r.environment.apiKey == "test-key-not-a-secret");
        assert(!r.environment.debugSinks);
    }

    WriteTempEnv(path,
        "SA_AGENT_BASE_URL=https://example.com/v1/chat/completions\n"
        "SA_AGENT_API_KEY=test-key-not-a-secret\n"
        "SA_AGENT_DEBUG_SINKS=1\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(r.ok);
        assert(r.environment.debugSinks);
        const AgentRuntimeEnvironment sink = LoadAgentDebugSinkDefaultsFromFile(path);
        assert(sink.debugSinks);
    }

    WriteTempEnv(path,
        "SA_AGENT_BASE_URL=https://example.com/v1/chat/completions\n"
        "SA_AGENT_API_KEY=test-key-not-a-secret\n"
        "SA_AGENT_DEBUG_SINKS=0\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(r.ok);
        assert(!r.environment.debugSinks);
        const AgentRuntimeEnvironment sink = LoadAgentDebugSinkDefaultsFromFile(path);
        assert(!sink.debugSinks);
    }

    WriteTempEnv(path,
        "SA_AGENT_BASE_URL=\"https://quoted.example/v1\"\n"
        "SA_AGENT_API_KEY='quoted-key'\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(r.ok);
        assert(r.environment.baseUrl == "https://quoted.example/v1");
        assert(r.environment.apiKey == "quoted-key");
    }

    WriteTempEnv(path,
        "SA_AGENT_BASE_URL=https://first.example\n"
        "SA_AGENT_API_KEY=first-key\n"
        "SA_AGENT_BASE_URL=https://second.example\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(r.ok);
        assert(r.environment.baseUrl == "https://second.example");
        assert(r.environment.apiKey == "first-key");
    }

    WriteTempEnv(path, "SA_AGENT_API_KEY=only-key\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(!r.ok);
        assert(r.error.find("SA_AGENT_BASE_URL") != std::string::npos);
    }

    WriteTempEnv(path, "SA_AGENT_BASE_URL=https://example.com\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(!r.ok);
        assert(r.error.find("SA_AGENT_API_KEY") != std::string::npos);
    }

    WriteTempEnv(path,
        "SA_AGENT_BASE_URL=https://example.com\n"
        "SA_AGENT_API_KEY=\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(!r.ok);
        assert(r.error.find("SA_AGENT_API_KEY") != std::string::npos);
    }

    WriteTempEnv(path, "not_a_valid_line\n");
    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile(path);
        assert(!r.ok);
        assert(r.error.find("format error") != std::string::npos);
    }

    {
        const AgentEnvLoadResult r = LoadAgentEnvironmentFromFile("nonexistent_agent_runtime_config.env");
        assert(!r.ok);
        assert(r.error.find("not found") != std::string::npos);
    }

    RemoveTemp(path);
    std::printf("All AgentRuntimeConfig tests passed.\n");
    return 0;
}
