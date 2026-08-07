/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/
#include <if_system_ability_manager.h>
#include "system_ability.h"
#include "iservice_registry.h"
#include "napi/native_api.h"
#include "napi/native_node_api.h"
#include <string>
#include "AgentServiceProxy.h"
#include <unistd.h>
#include "camera_agent_log.h"

using namespace OHOS::HiviewDFX;

namespace OHOS {
namespace CameraAgentServiceNapi {
static sptr<Multimedia::CameraAgentService::IAgentService> GetAgentProxy()
{
    sptr<ISystemAbilityManager> sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        CAMERA_AGENT_LOG_ERROR("GetAgentProxy get samgr failed");
        return nullptr;
    }
    sptr<IRemoteObject> remoteObject = sam->CheckSystemAbility(Multimedia::CameraAgentService::COMMUTE_AGENT_SERVICE_ID);
    if (remoteObject == nullptr) {
        remoteObject = sam->LoadSystemAbility(Multimedia::CameraAgentService::COMMUTE_AGENT_SERVICE_ID, 3000);
    }
    if (remoteObject == nullptr) {
        CAMERA_AGENT_LOG_ERROR("GetAgentProxy load sa failed");
        return nullptr;
    }
    return iface_cast<Multimedia::CameraAgentService::IAgentService>(remoteObject);
}

napi_value CheckServiceAbility(napi_env env, napi_callback_info info)
{
    CAMERA_AGENT_LOG_INFO("CheckServiceAbility napi come in");
    napi_value ret = nullptr;
    int res = 0;

    sptr<ISystemAbilityManager> sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        CAMERA_AGENT_LOG_INFO("called, CheckServiceAbility get SystemAbilityManager failed.");
        res = -1;
        napi_create_int32(env, res, &ret);
        return ret;
    }
    sptr<IRemoteObject> remoteObject = sam->CheckSystemAbility(Multimedia::CameraAgentService::COMMUTE_AGENT_SERVICE_ID);
    if (remoteObject == nullptr) {
        CAMERA_AGENT_LOG_INFO("called, CheckServiceAbility, ability is not exist");
        res = -1;
        napi_create_int32(env, res, &ret);
        return ret;
    }
    napi_create_int32(env, res, &ret);
    return ret;
}

napi_value InitService(napi_env env, napi_callback_info info)
{
    CAMERA_AGENT_LOG_INFO("InitService COME IN");
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string res = (proxy == nullptr) ? "load sa failed" : "init sa success";
    napi_create_string_utf8(env, res.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value HelloWorld(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string msg = (proxy == nullptr) ? "load sa failed" : proxy->HelloWorld();
    napi_create_string_utf8(env, msg.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value DestroyService(napi_env env, napi_callback_info info)
{
    CAMERA_AGENT_LOG_INFO(" DestroyService napi come in");
    napi_value ret = nullptr;
    std::string res = "";

    sptr<ISystemAbilityManager> sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        CAMERA_AGENT_LOG_INFO(" called, DestroyService get SystemAbilityManager failed.");
        res = "get samgr failed";
        napi_create_string_utf8(env, res.c_str(), NAPI_AUTO_LENGTH, &ret);
        return ret;
    }
    sptr<IRemoteObject> remoteObject = sam->GetSystemAbility(Multimedia::CameraAgentService::COMMUTE_AGENT_SERVICE_ID);
    if (remoteObject == nullptr) {
        CAMERA_AGENT_LOG_INFO("called, DestroyService get SystemAbility failed.");
        res = "get sa failed";
        napi_create_string_utf8(env, res.c_str(), NAPI_AUTO_LENGTH, &ret);
        return ret;
    }
    auto proxy = iface_cast<Multimedia::CameraAgentService::IAgentService>(remoteObject);
    proxy->UnLoadService();
    res = "destroy sa success";
    napi_create_string_utf8(env, res.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value StartSensorCollection(napi_env env, napi_callback_info info)
{
    (void)info;

    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StartSensorCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StopSensorCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StopSensorCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value GetRecentAccFrames(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string frames =
        (proxy == nullptr) ? "{\"acc\":null,\"gyro\":null,\"mag\":null,\"rv\":null,\"location\":null,\"baro\":null,\"wifi\":null,\"ble\":null,\"cells\":[]}" :
        proxy->GetRecentAccFrames();
    napi_create_string_utf8(env, frames.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value StartLocationCollection(napi_env env, napi_callback_info info)
{
    (void)info;

    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StartLocationCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StopLocationCollection(napi_env env, napi_callback_info info)
{
    (void)info;

    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StopLocationCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value DrainPdrResults(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string result = (proxy == nullptr) ? "[]" : proxy->DrainPdrResults();
    napi_create_string_utf8(env, result.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value ClearPdrResults(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->ClearPdrResults();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value GetPdrDiag(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string result = (proxy == nullptr) ? "{}" : proxy->GetPdrDiag();
    napi_create_string_utf8(env, result.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value GetLeaveCarState(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string result =
        (proxy == nullptr) ? "{\"ready\":false,\"userMode\":\"unknown\"}" : proxy->GetLeaveCarState();
    napi_create_string_utf8(env, result.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value GetStartPlaceInfo(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    const std::string result =
        (proxy == nullptr) ? "{\"ready\":false,\"hint\":\"sa_unavailable\"}" : proxy->GetStartPlaceInfo();
    napi_create_string_utf8(env, result.c_str(), NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value StartWifiCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StartWifiCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StopWifiCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StopWifiCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StartBleCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StartBleCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StopBleCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StopBleCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StartCellCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StartCellCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

napi_value StopCellCollection(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value ret = nullptr;
    auto proxy = GetAgentProxy();
    int32_t code = (proxy == nullptr) ? -1 : proxy->StopCellCollection();
    napi_create_int32(env, code, &ret);
    return ret;
}

EXTERN_C_START
static napi_value NapiInit(napi_env env, napi_value exports)
{
    napi_property_descriptor descriptors[] = {
        DECLARE_NAPI_FUNCTION("CheckServiceAbility", CheckServiceAbility),
        DECLARE_NAPI_FUNCTION("InitService", InitService),
        DECLARE_NAPI_FUNCTION("HelloWorld", HelloWorld),
        DECLARE_NAPI_FUNCTION("DestroyService", DestroyService),
        DECLARE_NAPI_FUNCTION("StartSensorCollection", StartSensorCollection),
        DECLARE_NAPI_FUNCTION("StopSensorCollection", StopSensorCollection),
        DECLARE_NAPI_FUNCTION("GetRecentAccFrames", GetRecentAccFrames),
        DECLARE_NAPI_FUNCTION("StartLocationCollection", StartLocationCollection),
        DECLARE_NAPI_FUNCTION("StopLocationCollection", StopLocationCollection),
        DECLARE_NAPI_FUNCTION("DrainPdrResults", DrainPdrResults),
        DECLARE_NAPI_FUNCTION("ClearPdrResults", ClearPdrResults),
        DECLARE_NAPI_FUNCTION("GetPdrDiag", GetPdrDiag),
        DECLARE_NAPI_FUNCTION("GetLeaveCarState", GetLeaveCarState),
        DECLARE_NAPI_FUNCTION("GetStartPlaceInfo", GetStartPlaceInfo),
        DECLARE_NAPI_FUNCTION("StartWifiCollection", StartWifiCollection),
        DECLARE_NAPI_FUNCTION("StopWifiCollection", StopWifiCollection),
        DECLARE_NAPI_FUNCTION("StartBleCollection", StartBleCollection),
        DECLARE_NAPI_FUNCTION("StopBleCollection", StopBleCollection),
        DECLARE_NAPI_FUNCTION("StartCellCollection", StartCellCollection),
        DECLARE_NAPI_FUNCTION("StopCellCollection", StopCellCollection),
    };

    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors));
    return exports;
}
EXTERN_C_END

static napi_module _module = { .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = "commuteagentservice",
    .nm_register_func = NapiInit,
    .nm_modname = "commuteagentservice",
    .nm_priv = ((void *)0),
    .reserved = { 0 } };

extern "C" __attribute__((constructor)) void RegisterModule(void)
{
    napi_module_register(&_module);
}

}
} // namespace OHOS
