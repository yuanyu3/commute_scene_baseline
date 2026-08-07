/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr common.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_COMMON_H_
#define XDR_XDR_COMMON_H_
#include "riemann_pdr_rotation.h"

namespace AIPDR {

namespace XDR {

constexpr double SMALL_VAL = 1e-6;

struct FxyzInfo {
    double x;
    double y;
    double z;
    long timestamp;
    FxyzInfo()
    {}
    FxyzInfo(double x, double y, double z, long timestamp) : x(x), y(y), z(z), timestamp(timestamp)
    {}
};

struct FrotInfo {
    long timestamp;
    Rotation rot;
    FrotInfo() : timestamp(0), rot(1.0, 0.0, 0.0, 0.0, true)
    {}
    FrotInfo(long t, const Rotation &q) : timestamp(t), rot(q)
    {}
    FrotInfo(long t, double q0, double q1, double q2, double q3)
        : timestamp(t), rot(q0, q1, q2, q3, true)
    {}
};
enum class SensorType { ACC, MAG, GYRO, GRV, RV };
}  // namespace XDR
}

#endif  // XDR_XDR_COMMON_H_
