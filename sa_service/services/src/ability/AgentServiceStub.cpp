/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/

#include "AgentServiceStub.h"
#include <cstdint>
#include <string>
#include "camera_agent_log.h"

using namespace OHOS::HiviewDFX;

namespace OHOS::Multimedia::CameraAgentService {

int32_t AgentServiceStub::OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply,
    MessageOption &option)
{
    (void)data;
    (void)option;
    if (data.ReadInterfaceToken() != GetDescriptor()) {
        CAMERA_AGENT_LOG_ERROR("Invalid interface token");
        return OHOS::ERR_INVALID_STATE;
    }
    switch (code)
    {
        case HELLO_WORLD: {
            CAMERA_AGENT_LOG_INFO("Received HELLO_WORLD command in stub");
            reply.WriteString(HelloWorld());
            return OHOS::NO_ERROR;
        }
        case UNLOAD_SERVICE: {
            CAMERA_AGENT_LOG_INFO("Received UNLOAD_SERVICE command in stub");
            UnLoadService();
            return OHOS::NO_ERROR;
        }
        case START_SENSOR_COLLECTION: {
            CAMERA_AGENT_LOG_INFO("Received START_SENSOR_COLLECTION command in stub");
            reply.WriteInt32(StartSensorCollection());
            return OHOS::NO_ERROR;
        }
        case STOP_SENSOR_COLLECTION: {
            CAMERA_AGENT_LOG_INFO("Received STOP_SENSOR_COLLECTION command in stub");
            reply.WriteInt32(StopSensorCollection());
            return OHOS::NO_ERROR;
        }
        case GET_RECENT_ACC_FRAMES: {
            reply.WriteString(GetRecentAccFrames());
            return OHOS::NO_ERROR;
        }
        case START_LOCATION_COLLECTION: {
            CAMERA_AGENT_LOG_INFO("Received START_LOCATION_COLLECTION command in stub");
            reply.WriteInt32(StartLocationCollection());
            return OHOS::NO_ERROR;
        }
        case STOP_LOCATION_COLLECTION: {
            CAMERA_AGENT_LOG_INFO("Received STOP_LOCATION_COLLECTION command in stub");
            reply.WriteInt32(StopLocationCollection());
            return OHOS::NO_ERROR;
        }
        case DRAIN_PDR_RESULTS: {
            reply.WriteString(DrainPdrResults());
            return OHOS::NO_ERROR;
        }
        case CLEAR_PDR_RESULTS: {
            reply.WriteInt32(ClearPdrResults());
            return OHOS::NO_ERROR;
        }
        case GET_PDR_DIAG: {
            reply.WriteString(GetPdrDiag());
            return OHOS::NO_ERROR;
        }
        case GET_LEAVE_CAR_STATE: {
            reply.WriteString(GetLeaveCarState());
            return OHOS::NO_ERROR;
        }
        case GET_START_PLACE_INFO: {
            reply.WriteString(GetStartPlaceInfo());
            return OHOS::NO_ERROR;
        }
        case START_WIFI_COLLECTION: {
            reply.WriteInt32(StartWifiCollection());
            return OHOS::NO_ERROR;
        }
        case STOP_WIFI_COLLECTION: {
            reply.WriteInt32(StopWifiCollection());
            return OHOS::NO_ERROR;
        }
        case START_BLE_COLLECTION: {
            reply.WriteInt32(StartBleCollection());
            return OHOS::NO_ERROR;
        }
        case STOP_BLE_COLLECTION: {
            reply.WriteInt32(StopBleCollection());
            return OHOS::NO_ERROR;
        }
        case START_CELL_COLLECTION: {
            reply.WriteInt32(StartCellCollection());
            return OHOS::NO_ERROR;
        }
        case STOP_CELL_COLLECTION: {
            reply.WriteInt32(StopCellCollection());
            return OHOS::NO_ERROR;
        }
        default:
            break;
    }
    return 0;
}
}