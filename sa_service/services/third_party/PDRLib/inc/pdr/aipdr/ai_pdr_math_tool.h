/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: mipt pdr sensor process.
 * Author: w00623447
 * Create: 2023-02-01
 */

#ifndef CORE_AI_PDR_MATH_TOOL_H
#define CORE_AI_PDR_MATH_TOOL_H

#include "ai_pdr_common_define.h"

namespace AIPDR {
struct IMUSensorData {
    long long timestamp;  // ms
    float x;
    float y;
    float z;
};  // IMU data include x axis, y axis, z axis

struct SensorBuffer {
    IMUSensorData sensorBuffer[SENSOR_BUFF_LEN];  // at most store 600 sensor data
    int count;
} ;  // sensor buffer include IMU data array, count of data

struct NNModel_ {
    bool modelLoadFlag;  // false-not loaded, true-loaded
};               // model struct

using NNModel = NNModel_;

struct Quaternion {
    long long timestamp;
    float x;
    float y;
    float z;
    float w;
};

struct Quaternionl {
    long long timestamp;
    double x;
    double y;
    double z;
    double w;
};

struct QuaternionBuffer_ {
    Quaternion dataBuffer[SENSOR_BUFF_LEN];  // at most store 600 sensor data
    int count;
};  // quaternion buffer include quaternion data array, count of data

using QuaternionBuffer = QuaternionBuffer_;

struct RotationMatBuffer_ {
    double dataBuffer[SENSOR_BUFF_LEN][3][3];  // at most store 600 sensor data, 3*3 dim
    double yaw[SENSOR_BUFF_LEN];
    long long timestamp[SENSOR_BUFF_LEN];
    int count;
};  // Ri_T,i->n

using RotationMatBuffer = RotationMatBuffer_;

struct Vector3D {
    float x;
    float y;
    float z;
};
}
#endif  // CORE_AI_PDR_MATH_TOOL_H
