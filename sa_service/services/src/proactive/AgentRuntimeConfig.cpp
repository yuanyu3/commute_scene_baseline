/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Minimal dotenv parser for SA Agent runtime credentials.
 */
#include "AgentRuntimeConfig.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace OHOS::Multimedia::CameraAgentService {
namespace {

std::string Trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string Unquote(const std::string &value)
{
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool ParseLine(const std::string &line, std::string &keyOut, std::string &valueOut, std::string &errorOut)
{
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
        return false;
    }

    const size_t eq = trimmed.find('=');
    if (eq == std::string::npos) {
        errorOut = "invalid line (missing '=')";
        return false;
    }

    keyOut = Trim(trimmed.substr(0, eq));
    valueOut = Unquote(Trim(trimmed.substr(eq + 1)));
    if (keyOut.empty()) {
        errorOut = "invalid line (empty key)";
        return false;
    }
    return true;
}

bool ParseEnvBool(const std::string &v, bool defaultValue)
{
    if (v == "0" || v == "false" || v == "FALSE" || v == "off" || v == "OFF" || v == "no" || v == "NO") {
        return false;
    }
    if (v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON" || v == "yes" || v == "YES") {
        return true;
    }
    return defaultValue;
}

bool ReadEnvMap(const std::string &path, std::unordered_map<std::string, std::string> &values, std::string &errorOut)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        errorOut = "agent environment file not found";
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::string key;
        std::string value;
        std::string parseError;
        if (!ParseLine(line, key, value, parseError)) {
            if (!parseError.empty()) {
                std::ostringstream oss;
                oss << "agent environment file format error at line " << lineNumber << ": " << parseError;
                errorOut = oss.str();
                return false;
            }
            continue;
        }
        values[key] = value;
    }
    return true;
}

void ApplyOptionalFlags(const std::unordered_map<std::string, std::string> &values, AgentRuntimeEnvironment &env)
{
    env.debugSinks = false;
    const auto dbg = values.find(kAgentEnvKeyDebugSinks);
    if (dbg != values.end()) {
        env.debugSinks = ParseEnvBool(dbg->second, false);
    }
}

} // namespace

AgentEnvLoadResult LoadAgentEnvironmentFromFile(const std::string &path)
{
    AgentEnvLoadResult result;
    std::unordered_map<std::string, std::string> values;
    if (!ReadEnvMap(path, values, result.error)) {
        return result;
    }

    const auto baseIt = values.find(kAgentEnvKeyBaseUrl);
    if (baseIt == values.end() || baseIt->second.empty()) {
        result.error = "missing SA_AGENT_BASE_URL";
        return result;
    }

    const auto keyIt = values.find(kAgentEnvKeyApiKey);
    if (keyIt == values.end() || keyIt->second.empty()) {
        result.error = "missing SA_AGENT_API_KEY";
        return result;
    }

    result.environment.baseUrl = baseIt->second;
    result.environment.apiKey = keyIt->second;
    ApplyOptionalFlags(values, result.environment);
    result.ok = true;
    return result;
}

AgentRuntimeEnvironment LoadAgentDebugSinkDefaultsFromFile(const std::string &path)
{
    AgentRuntimeEnvironment env;
    std::unordered_map<std::string, std::string> values;
    std::string error;
    if (!ReadEnvMap(path, values, error)) {
        return env;
    }
    ApplyOptionalFlags(values, env);
    return env;
}

} // namespace OHOS::Multimedia::CameraAgentService
