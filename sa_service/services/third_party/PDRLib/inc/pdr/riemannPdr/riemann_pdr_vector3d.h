/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_VECTOR3D_H_
#define XDR_XDR_INCLUDE_VECTOR3D_H_

#include <stdexcept>
#include <iostream>
#include "pdr/aipdr/ai_pdr_common_define.h"

namespace AIPDR {
namespace XDR {
class Vector3D {
public:
    // 静态常量向量
    static const Vector3D ZERO;
    static const Vector3D PLUS_I;
    static const Vector3D MINUS_I;
    static const Vector3D PLUS_J;
    static const Vector3D MINUS_J;
    static const Vector3D PLUS_K;
    static const Vector3D MINUS_K;
    static const Vector3D NaN;
    static const Vector3D POSITIVE_INFINITY;
    static const Vector3D NEGATIVE_INFINITY;

    // 构造函数
    explicit Vector3D(double x = 0, double y = 0, double z = 0) noexcept;
    Vector3D(double a, const Vector3D &u);
    Vector3D(double a1, const Vector3D &u1, double a2, const Vector3D &u2);

    // 获取向量的坐标
    double GetX() const;
    double GetY() const;
    double GetZ() const;
    void ToArray(double v[THREE]) const;

    // 获取向量的各种范数
    double GetNorm1() const;
    double GetNorm() const;
    double GetNormSq() const;
    double GetNormInf() const;

    // 获取向量的方位角和仰角
    double GetAlpha() const;
    double GetDelta() const;

    // 向量的加法和减法
    Vector3D Add(const Vector3D &v) const;
    Vector3D Add(double factor, const Vector3D &v) const;
    Vector3D Subtract(const Vector3D &v) const;
    Vector3D Subtract(double factor, const Vector3D &v) const;

    // 向量归一化
    Vector3D Normalize() const;

    // 向量数乘
    Vector3D ScalarMultiply(double a) const;

    bool IsInfinite() const;

    // 向量相等性比较
    bool Equals(const Vector3D &other) const;

    // 计算向量的点积和叉积
    double DotProduct(const Vector3D &v) const;
    Vector3D CrossProduct(const Vector3D &v) const;

    // 计算向量之间的距离
    double Distance(const Vector3D &v) const;

    // 静态方法计算点积、叉积和距离
    static double DotProduct(const Vector3D &v1, const Vector3D &v2);
    static Vector3D CrossProduct(const Vector3D &v1, const Vector3D &v2);
    static double Distance1(const Vector3D &v1, const Vector3D &v2);
    static double Distance(const Vector3D &v1, const Vector3D &v2);
    static double DistanceInf(const Vector3D &v1, const Vector3D &v2);
    static double DistanceSq(const Vector3D &v1, const Vector3D &v2);

private:
    double x;
    double y;
    double z;
};

// 重载输出流运算符，方便打印向量
std::ostream &operator<<(std::ostream &os, const Vector3D &v);
}  // namespace XDR

}

#endif  // XDR_XDR_INCLUDE_VECTOR3D_H_
