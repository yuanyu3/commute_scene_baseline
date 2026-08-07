/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr pdr result.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_PDR_RESULT_H_
#define XDR_XDR_INCLUDE_PDR_RESULT_H_
#include <string>
#include <cmath>
#include <optional>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

#include "riemann_pdr_rotation.h"
#include "pdr/aipdr/ai_pdr_utils.h"

namespace AIPDR {

constexpr uint32_t HASH_VAL = 0x9e3779b9;

namespace XDR {

// 定义 MotionStatus 枚举
enum class MotionStatus { MOVE, STAY, MAGS_ABNORMAL, OFF_TRAJECTORY };

class PdrResult {
public:
    // 构造函数
    PdrResult() = default;

    PdrResult(double xValue, double yValue) : PdrResult(xValue, yValue, 0.0)
    {}

    PdrResult(double xValue, double yValue, double zValue)
    {
        m_px = xValue;
        m_py = yValue;
        m_pz = zValue;
    }
    // 获取时间戳
    long GetTimestamp() const
    {
        return m_timestamp;
    }
    // 设置时间戳
    void SetTimestamp(long timestamp)
    {
        m_timestamp = timestamp;
    }
    // 获取启动时间
    long GetBootTime() const
    {
        return m_bootTime;
    }
    // 设置启动时间
    void SetBootTime(long bootTime)
    {
        m_bootTime = bootTime;
    }
    // 获取 pX 值
    double GetPX() const
    {
        return m_px;
    }
    // 设置 pX 值
    void SetPX(double pX)
    {
        m_px = pX;
    }
    // 获取 pY 值
    double GetPY() const
    {
        return m_py;
    }
    // 设置 pY 值
    void SetPY(double pY)
    {
        m_py = pY;
    }
    // 获取 pZ 值
    double GetPZ() const
    {
        return m_pz;
    }
    // 设置 pZ 值
    void SetPZ(double pZ)
    {
        m_pz = pZ;
    }
    // 获取 vx 值
    double GetVx() const
    {
        return m_vx;
    }
    // 设置 vx 值
    void SetVx(double vx)
    {
        m_vx = vx;
    }
    // 获取 vy 值
    double GetVy() const
    {
        return m_vy;
    }
    // 设置 vy 值
    void SetVy(double vy)
    {
        m_vy = vy;
    }
    // 获取 vz 值
    double GetVz() const
    {
        return m_vz;
    }
    // 设置 vz 值
    void SetVz(double vz)
    {
        m_vz = vz;
    }
    // 获取方位角
    double GetAzimuth() const
    {
        return m_azimuth;
    }
    // 设置方位角
    void SetAzimuth(double azimuth)
    {
        m_azimuth = azimuth;
    }
    // 获取精度
    double GetAccuracy() const
    {
        return m_accuracy;
    }
    // 设置精度
    void SetAccuracy(double accuracy)
    {
        m_accuracy = accuracy;
    }
    // 获取错误码
    int GetErrorCode() const
    {
        return m_errorCode;
    }
    // 设置错误码
    void SetErrorCode(int errorCode)
    {
        m_errorCode = errorCode;
    }
    // 检查方向是否初始化
    bool IsDirectionInitialized() const
    {
        return m_isDirectionInitialized;
    }
    // 设置方向是否初始化
    void SetDirectionInitialized(bool isDirectionInitialized)
    {
        m_isDirectionInitialized = isDirectionInitialized;
    }
    // 获取校正后的 x 值
    double GetCx() const
    {
        return m_cx;
    }
    // 设置校正后的 x 值
    void SetCx(double cx)
    {
        m_cx = cx;
    }
    // 获取校正后的 y 值
    double GetCy() const
    {
        return m_cy;
    }
    // 设置校正后的 y 值
    void SetCy(double cy)
    {
        m_cy = cy;
    }
    // 获取 refId
    std::string GetRefId() const
    {
        return m_refId;
    }
    // 设置 refId
    void SetRefId(const std::string &refId)
    {
        m_refId = refId;
    }
    // 获取旋转角度
    double GetRotationTheta() const
    {
        return m_rotationTheta;
    }
    // 设置旋转角度
    void SetRotationTheta(double rotationTheta)
    {
        m_rotationTheta = rotationTheta;
    }
    // 获取运动状态
    MotionStatus GetMotionStatus() const
    {
        return m_motionStatus;
    }
    // 设置运动状态
    void SetMotionStatus(MotionStatus status)
    {
        m_motionStatus = status;
    }
    // 获取 DeltaT
    double GetDeltaT() const
    {
        return delta_t_;
    }
    // 设置 DeltaT
    void SetDeltaT(double deltaT)
    {
        delta_t_ = deltaT;
    }
    // 计算二维距离
    double Dist2D(const PdrResult &pdrRes) const
    {
        if (pdrRes.GetTimestamp() == 0L) {
            return 0.0;
        }
        return std::sqrt(std::pow(m_px - pdrRes.GetPX(), TWO) + std::pow(m_py - pdrRes.GetPY(), TWO));
    }
    // 转换为字符串
    std::string ToString() const
    {
        std::ostringstream oss;
        oss << m_timestamp << "," << m_bootTime << "," << m_px << "," << m_py << "," << m_pz << "," << m_cx << ","
            << m_cy << "," << m_vx << "," << m_vy << "," << m_vz << "," << m_azimuth << "," << m_accuracy << ","
            << m_errorCode << "," << m_isDirectionInitialized << "," << m_refId << "," << m_rotationTheta;
        return oss.str();
    }
    // 重写哈希函数
    size_t HashCode() const
    {
        size_t seed = 0;
        auto combine = [&seed](auto value) {
            seed ^= std::hash<decltype(value)>()(value) + HASH_VAL + (seed << SIX) + (seed >> TWO);
        };
        combine(m_timestamp);
        combine(m_bootTime);
        combine(m_px);
        combine(m_py);
        combine(m_pz);
        combine(m_cx);
        combine(m_cy);
        combine(m_vx);
        combine(m_vy);
        combine(m_vz);
        combine(m_azimuth);
        combine(m_accuracy);
        combine(m_errorCode);
        combine(m_isDirectionInitialized);
        combine(m_refId);
        combine(m_rotationTheta);
        combine(static_cast<int>(m_motionStatus));
        return seed;
    }
    // 重写相等比较函数
    bool Equals(const PdrResult &other) const
    {
        return m_timestamp == other.m_timestamp && m_bootTime == other.m_bootTime && IsDoubleEqual(m_px, other.m_px) &&
               IsDoubleEqual(m_py, other.m_py) && IsDoubleEqual(m_pz, other.m_pz) && IsDoubleEqual(m_cx, other.m_cx) &&
               IsDoubleEqual(m_cy, other.m_cy) && IsDoubleEqual(m_vx, other.m_vx) && IsDoubleEqual(m_vy, other.m_vy) &&
               IsDoubleEqual(m_vz, other.m_vz) && IsDoubleEqual(m_azimuth, other.m_azimuth) &&
               IsDoubleEqual(m_accuracy, other.m_accuracy) && m_errorCode == other.m_errorCode &&
               m_isDirectionInitialized == other.m_isDirectionInitialized && m_refId == other.m_refId &&
               IsDoubleEqual(m_rotationTheta, other.m_rotationTheta) && m_motionStatus == other.m_motionStatus;
    }
    // 克隆函数
    PdrResult Clone() const
    {
        PdrResult newResult = *this;
        return newResult;
    }
    void SetQwi(const Rotation &q)
    {
        m_qWiCorr = q;
    }
    Rotation GetQwi() const
    {
        return m_qWiCorr;
    }
    bool IsValid() const
    {
        return m_timestamp != 0;
    }

private:
    long m_timestamp = 0L;
    long m_bootTime = 0L;
    double m_px = 0.0;
    double m_py = 0.0;
    double m_pz = 0.0;
    double m_vx = 0.0;
    double m_vy = 0.0;
    double m_vz = 0.0;
    double m_azimuth = 0.0;
    double m_accuracy = 0.0;
    int m_errorCode = 0;
    bool m_isDirectionInitialized = false;
    double m_rotationTheta = 0.0;
    std::string m_refId;
    double m_cx = 0.0;
    double m_cy = 0.0;
    MotionStatus m_motionStatus = MotionStatus::STAY;
    double delta_t_ = 0.0;

    Rotation m_qWiCorr;
};
}  // namespace XDR

}

#endif  // XDR_XDR_INCLUDE_PDR_RESULT_H_
