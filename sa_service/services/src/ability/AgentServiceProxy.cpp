/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/

#include "AgentServiceProxy.h"
#include "hilog/log.h"
#include <cstdint>
#include <unistd.h>
#include <string>
#include "camera_agent_log.h"

using namespace OHOS::HiviewDFX;

namespace OHOS::Multimedia::CameraAgentService {

AgentServiceProxy::AgentServiceProxy(const sptr<IRemoteObject> &object)
    : IRemoteProxy<IAgentService>(object)
{

}

std::string AgentServiceProxy::HelloWorld()
{
    CAMERA_AGENT_LOG_INFO("HelloWorld called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(HELLO_WORLD, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("HelloWorld SendRequest failed, error code: %{public}d", error);
        return "";
    }
    return reply.ReadString();
}

void AgentServiceProxy::UnLoadService()
{
    CAMERA_AGENT_LOG_INFO("UnLoadService called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(UNLOAD_SERVICE, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("UnLoadService SendRequest failed, error code: %{public}d", error);
    }
    CAMERA_AGENT_LOG_INFO("proxy send UnLoadService request successful");
}

int32_t AgentServiceProxy::StartSensorCollection()
{
    CAMERA_AGENT_LOG_INFO("StartSensorCollection called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(START_SENSOR_COLLECTION, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("StartSensorCollection SendRequest failed, error code: %{public}d", error);
        return -1;
    }
    return reply.ReadInt32();
}

int32_t AgentServiceProxy::StopSensorCollection()
{
    CAMERA_AGENT_LOG_INFO("StopSensorCollection called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(STOP_SENSOR_COLLECTION, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("StopSensorCollection SendRequest failed, error code: %{public}d", error);
        return -1;
    }
    return reply.ReadInt32();
}

int32_t AgentServiceProxy::StartLocationCollection()
{
    CAMERA_AGENT_LOG_INFO("StartLocationCollection called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(START_LOCATION_COLLECTION, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("StartLocationCollection SendRequest failed, error code: %{public}d", error);
        return -1;
    }
    return reply.ReadInt32();
}

int32_t AgentServiceProxy::StopLocationCollection()
{
    CAMERA_AGENT_LOG_INFO("StopLocationCollection called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(STOP_LOCATION_COLLECTION, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("StopLocationCollection SendRequest failed, error code: %{public}d", error);
        return -1;
    }
    return reply.ReadInt32();
}

std::string AgentServiceProxy::GetRecentAccFrames()
{
    CAMERA_AGENT_LOG_INFO("GetRecentAccFrames called in AgentServiceProxy");
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(GET_RECENT_ACC_FRAMES, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("GetRecentAccFrames SendRequest failed, error code: %{public}d", error);
        return "{\"acc\":null,\"gyro\":null,\"mag\":null,\"rv\":null,\"location\":null,\"baro\":null,\"wifi\":null,\"ble\":null,\"cells\":[]}";
    }
    return reply.ReadString();
}

std::string AgentServiceProxy::DrainPdrResults()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(DRAIN_PDR_RESULTS, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("DrainPdrResults SendRequest failed, error code: %{public}d", error);
        return "[]";
    }
    return reply.ReadString();
}

int32_t AgentServiceProxy::ClearPdrResults()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(CLEAR_PDR_RESULTS, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("ClearPdrResults SendRequest failed, error code: %{public}d", error);
        return -1;
    }
    return reply.ReadInt32();
}

std::string AgentServiceProxy::GetPdrDiag()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(GET_PDR_DIAG, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("GetPdrDiag SendRequest failed, error code: %{public}d", error);
        return "{}";
    }
    return reply.ReadString();
}

std::string AgentServiceProxy::GetLeaveCarState()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(GET_LEAVE_CAR_STATE, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("GetLeaveCarState SendRequest failed, error code: %{public}d", error);
        return "{\"ready\":false,\"userMode\":\"unknown\"}";
    }
    return reply.ReadString();
}

std::string AgentServiceProxy::GetStartPlaceInfo()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(GET_START_PLACE_INFO, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("GetStartPlaceInfo SendRequest failed, error code: %{public}d", error);
        return "{\"ready\":false,\"hint\":\"ipc_failed\"}";
    }
    return reply.ReadString();
}

int32_t AgentServiceProxy::StartWifiCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(START_WIFI_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

int32_t AgentServiceProxy::StopWifiCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(STOP_WIFI_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

int32_t AgentServiceProxy::StartBleCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(START_BLE_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

int32_t AgentServiceProxy::StopBleCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(STOP_BLE_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

int32_t AgentServiceProxy::StartCellCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(START_CELL_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

int32_t AgentServiceProxy::StopCellCollection()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(STOP_CELL_COLLECTION, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}

std::string AgentServiceProxy::GetProductDebugTimeline()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(GET_PRODUCT_DEBUG_TIMELINE, data, reply, option);
    if (error != OHOS::NO_ERROR) {
        CAMERA_AGENT_LOG_ERROR("GetProductDebugTimeline SendRequest failed, error code: %{public}d", error);
        return "{\"ok\":false,\"error\":\"send_request_failed\"}";
    }
    return reply.ReadString();
}

int32_t AgentServiceProxy::ClearProductDebugTimeline()
{
    MessageParcel data, reply;
    MessageOption option(MessageOption::TF_SYNC);
    data.WriteInterfaceToken(GetDescriptor());
    int error = Remote()->SendRequest(CLEAR_PRODUCT_DEBUG_TIMELINE, data, reply, option);
    return (error != OHOS::NO_ERROR) ? -1 : reply.ReadInt32();
}
}