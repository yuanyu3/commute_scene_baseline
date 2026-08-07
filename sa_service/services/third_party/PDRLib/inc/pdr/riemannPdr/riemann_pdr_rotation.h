/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_ROTATION_H_
#define XDR_XDR_INCLUDE_ROTATION_H_

#include "riemann_pdr_vector3d.h"

namespace AIPDR {
namespace XDR {

// 定义 RotationConvention 枚举
enum class RotationConvention { VECTOR_OPERATOR };

class Rotation {
public:
    // 静态常量：单位旋转
    static const Rotation IDENTITY;
    // 构造函数
    Rotation();
    Rotation(double q0, double q1, double q2, double q3, bool needsNormalization) noexcept;
    explicit Rotation(const Vector3D &rotVec);
    Rotation(const Vector3D &axis, double angle);
    Rotation(const Vector3D &axis, double angle, RotationConvention convention);
    Rotation(const Vector3D &u, const Vector3D &v);
    // 获取四元数的各个分量
    double getQ0() const;
    double getQ1() const;
    double getQ2() const;
    double getQ3() const;
    // 对向量应用旋转
    Vector3D applyTo(const Vector3D &u) const;
    Rotation applyTo(const Rotation &r) const;
    Rotation compose(const Rotation &r, RotationConvention convention) const;

private:
    Rotation composeInternal(const Rotation &r) const;
    double q0 = 1.0;
    double q1 = 0.0;
    double q2 = 0.0;
    double q3 = 0.0;
};

}  // namespace XDR
}

#endif  // XDR_XDR_INCLUDE_ROTATION_H_
