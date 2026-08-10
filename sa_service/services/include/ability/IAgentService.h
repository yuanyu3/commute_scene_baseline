/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: 00435237
* Create: 2024/06/05
*/

#ifndef IAGENTSERVICE_H
#define IAGENTSERVICE_H

#include "iremote_broker.h"
#include "hilog/log.h"
#include <cstdint>
#include <string>

namespace OHOS::Multimedia::CameraAgentService {
constexpr int32_t COMMUTE_AGENT_SERVICE_ID = 9903;

class IAgentService : public IRemoteBroker {
public:
    enum {
        HELLO_WORLD = 0,
        UNLOAD_SERVICE = 1,
        START_SENSOR_COLLECTION = 2,
        STOP_SENSOR_COLLECTION = 3,
        GET_RECENT_ACC_FRAMES = 4,
        START_LOCATION_COLLECTION = 5,
        STOP_LOCATION_COLLECTION = 6,
        DRAIN_PDR_RESULTS = 7,
        CLEAR_PDR_RESULTS = 8,
        GET_PDR_DIAG = 9,
        GET_LEAVE_CAR_STATE = 10,
        GET_START_PLACE_INFO = 11,
        START_WIFI_COLLECTION = 12,
        STOP_WIFI_COLLECTION = 13,
        START_BLE_COLLECTION = 14,
        STOP_BLE_COLLECTION = 15,
        START_CELL_COLLECTION = 16,
        STOP_CELL_COLLECTION = 17,
        GET_PRODUCT_DEBUG_TIMELINE = 18,
        CLEAR_PRODUCT_DEBUG_TIMELINE = 19,
    };

    virtual std::string HelloWorld() = 0;
    virtual void UnLoadService() = 0;
    virtual int32_t StartSensorCollection() = 0;
    virtual int32_t StopSensorCollection() = 0;
    virtual std::string GetRecentAccFrames() = 0;
    virtual int32_t StartLocationCollection() = 0;
    virtual int32_t StopLocationCollection() = 0;
    virtual std::string DrainPdrResults() = 0;
    virtual int32_t ClearPdrResults() = 0;
    virtual std::string GetPdrDiag() = 0;
    virtual std::string GetLeaveCarState() = 0;
    virtual std::string GetStartPlaceInfo() = 0;
    virtual int32_t StartWifiCollection() = 0;
    virtual int32_t StopWifiCollection() = 0;
    virtual int32_t StartBleCollection() = 0;
    virtual int32_t StopBleCollection() = 0;
    virtual int32_t StartCellCollection() = 0;
    virtual int32_t StopCellCollection() = 0;
    virtual std::string GetProductDebugTimeline() = 0;
    virtual int32_t ClearProductDebugTimeline() = 0;

    DECLARE_INTERFACE_DESCRIPTOR(u"ohos.agent.agentservice.IAgentService");
};
}
#endif
