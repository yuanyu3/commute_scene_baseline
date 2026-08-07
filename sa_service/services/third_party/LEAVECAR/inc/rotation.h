#ifndef XDR_XDR_INCLUDE_ROTATION_H_
#define XDR_XDR_INCLUDE_ROTATION_H_

#include <cmath>
#include <stdexcept>
#include <vector>
#include "vector3d.h"

namespace XDR {

// 定义 RotationConvention 枚举
enum class RotationConvention {
    VECTOR_OPERATOR
};

class Rotation {
public:
    // 静态常量：单位旋转
    static const Rotation IDENTITY;

    // 构造函数
    Rotation();
    Rotation(double q0, double q1, double q2, double q3, bool needsNormalization);
    Rotation(const Vector3D& rot_vec);
    Rotation(const Vector3D& axis, double angle);
    Rotation(const Vector3D& axis, double angle, RotationConvention convention);
    Rotation(const Vector3D& u, const Vector3D& v);

    // 获取四元数的各个分量
    double getQ0() const;
    double getQ1() const;
    double getQ2() const;
    double getQ3() const;

    // 获取对应的 3x3 矩阵
    std::vector<std::vector<double>> getMatrix() const;

    // 对向量应用旋转
    Vector3D applyTo(const Vector3D& u) const;
    Rotation applyTo(const Rotation& r) const;
    Rotation compose(const Rotation& r, RotationConvention convention) const;
    
    private:
    Rotation composeInternal(const Rotation& r) const;
    
    double q0;
    double q1;
    double q2;
    double q3;
};

} // namespace XDR


#endif  // XDR_XDR_INCLUDE_ROTATION_H_
