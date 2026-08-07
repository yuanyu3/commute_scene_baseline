/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * Completes forward-declared jiuwen types needed to instantiate AnyValue on host tests.
 * Must be included before AnyValue.h / AgentStruct.h in host-only TUs.
 */
#ifndef SA_AGENT_HOST_TYPE_COMPLETIONS_H
#define SA_AGENT_HOST_TYPE_COMPLETIONS_H

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace jiuwen {

struct BaseMessage {
    virtual ~BaseMessage() = default;
};

struct ParametersSchema {
    std::string name;
    std::string description;
    std::string param;
    bool isRequired{false};
};

// Minimal complete ToolInfo so AnyValue's variant alternative can be instantiated.
// Field layout does not need to match the device library for host config tests.
struct ToolInfo {
    std::string name;
    std::string description;
    std::vector<std::tuple<std::string, std::string, std::string, bool>> params;
    ParametersSchema parametersSchema;
};

}  // namespace jiuwen

#endif  // SA_AGENT_HOST_TYPE_COMPLETIONS_H
