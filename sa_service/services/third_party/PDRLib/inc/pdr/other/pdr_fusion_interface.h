/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * Description: ai-pdr mag rotation Fusion.
 * Author: z00838343
 * Create: 2024/12/23
 */

#ifndef __PDRFUSIONINTERFACE_H__
#define __PDRFUSIONINTERFACE_H__
#include "higeo_interface_fused_manager.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef struct {
    char validFlag;
    char hasLastHead;

    float lastHead;
    float distance;
    float angularChange;

    float speed[2];
    float lastSpeed[2];
    long timeStamp;

    float speedNorm; // 标量速度
    double position[2];
} MiptPdrData;

typedef enum {
    PDR_GPS_INVALID = 0x00000001,
    PDR_GPS_TIME_ERROR = 0x00000002,
    PDR_PDR_TIME_ERROR = 0x00000004,
    PDR_PDR_NOT_INITIAL = 0x00000008,
    PDR_SENSOR_DATA_COUNT_INVALID = 0x00000010,
    PDR_ACC_OR_GYRO_INVALID = 0x00000020,
    PDR_DATA_ADRESS_EMPTY = 0x00000040,
    PDR_DATA_FREQUENCY_ERROR = 0x00000080,
    PDR_ACC_VALUE_ERROR = 0x00000100,
    PDR_GYRO_VALUE_ERROR = 0x00000200,
    PDR_MAG_VALUE_ERROR = 0x00000400,
    PDR_TOO_MANY_STEPS = 0x00000800,
    PDR_GYRO_LOSS = 0x00001000,
    PDR_MAG_LOSS = 0x00002000,
    PDR_POSITION_MEASUREMENT_MISCONVERGENCE = 0x00004000,
    PDR_BEARING_MEASUREMENT_MISCONVERGENCE = 0x00008000
} PDR_ERROR;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif
