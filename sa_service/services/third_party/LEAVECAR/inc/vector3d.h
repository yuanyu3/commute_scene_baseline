#ifndef XDR_XDR_INCLUDE_VECTOR3D_H_
#define XDR_XDR_INCLUDE_VECTOR3D_H_

#include <cmath>
#include <stdexcept>
#include <iostream>

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
    Vector3D(double x = 0, double y = 0, double z = 0);
    Vector3D(const double v[3]);
    Vector3D(double alpha, double delta);
    Vector3D(double a, const Vector3D& u);
    Vector3D(double a1, const Vector3D& u1, double a2, const Vector3D& u2);
    Vector3D(double a1, const Vector3D& u1, double a2, const Vector3D& u2, double a3, const Vector3D& u3);
    Vector3D(double a1, const Vector3D& u1, double a2, const Vector3D& u2, double a3, const Vector3D& u3, double a4, const Vector3D& u4);

    // 获取向量的坐标
    double getX() const;
    double getY() const;
    double getZ() const;
    void toArray(double v[3]) const;

    // 获取向量的各种范数
    double getNorm1() const;
    double getNorm() const;
    double getNormSq() const;
    double getNormInf() const;

    // 获取向量的方位角和仰角
    double getAlpha() const;
    double getDelta() const;

    // 向量的加法和减法
    Vector3D add(const Vector3D& v) const;
    Vector3D add(double factor, const Vector3D& v) const;
    Vector3D subtract(const Vector3D& v) const;
    Vector3D subtract(double factor, const Vector3D& v) const;

    // 向量归一化
    Vector3D normalize() const;

    // 获取与当前向量正交的向量
    Vector3D orthogonal() const;

    // 计算两个向量的夹角
    static double angle(const Vector3D& v1, const Vector3D& v2);

    // 向量取反
    Vector3D negate() const;

    // 向量数乘
    Vector3D scalarMultiply(double a) const;

    // 检查向量是否为 NaN 或无穷大
    bool isNaN() const;
    bool isInfinite() const;

    // 向量相等性比较
    bool equals(const Vector3D& other) const;

    // 计算向量的点积和叉积
    double dotProduct(const Vector3D& v) const;
    Vector3D crossProduct(const Vector3D& v) const;

    // 计算向量之间的距离
    double distance1(const Vector3D& v) const;
    double distance(const Vector3D& v) const;
    double distanceInf(const Vector3D& v) const;
    double distanceSq(const Vector3D& v) const;

    // 静态方法计算点积、叉积和距离
    static double dotProduct(const Vector3D& v1, const Vector3D& v2);
    static Vector3D crossProduct(const Vector3D& v1, const Vector3D& v2);
    static double distance1(const Vector3D& v1, const Vector3D& v2);
    static double distance(const Vector3D& v1, const Vector3D& v2);
    static double distanceInf(const Vector3D& v1, const Vector3D& v2);
    static double distanceSq(const Vector3D& v1, const Vector3D& v2);

private:
    double x;
    double y;
    double z;
};

// 重载输出流运算符，方便打印向量
std::ostream& operator<<(std::ostream& os, const Vector3D& v);
} // namespace XDR


#endif  // XDR_XDR_INCLUDE_VECTOR3D_H_
