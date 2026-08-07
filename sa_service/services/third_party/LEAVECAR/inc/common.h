#ifndef XDR_XDR_COMMON_H_
#define XDR_XDR_COMMON_H_

#include "rotation.h"
namespace XDR {

struct FxyzInfo {
    double x;
    double y;
    double z;
    long timestamp;
    FxyzInfo() {}
    FxyzInfo(double x, double y, double z, long timestamp) : x(x), y(y), z(z), timestamp(timestamp) {}
};

struct FrotInfo {
    long timestamp;
    Rotation rot;
    FrotInfo() : rot(1.0, 0.0, 0.0, 0.0, true) {}
    FrotInfo(long t, const Rotation& q) : timestamp(t), rot(q) {}
    FrotInfo(long t, double q0, double q1, double q2, double q3, bool needsNormalization)
    : timestamp(t), rot(q0, q1, q2, q3, needsNormalization) {}
};

enum class SensorType {
    ACC,
    MAG,
    GYRO,
    GRV,
    RV
};

} // namespace XDR


#endif  // XDR_XDR_COMMON_H_
