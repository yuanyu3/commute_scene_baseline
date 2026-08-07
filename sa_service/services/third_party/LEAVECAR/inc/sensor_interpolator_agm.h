#ifndef XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_AG_H_
#define XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_AG_H_

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
#include "common.h"

namespace XDR {

struct InterpedDataPackAGM {
    std::vector<FxyzInfo> accs;
    std::vector<FxyzInfo> gyros;
    std::vector<FxyzInfo> mags;
};

// 模板函数，根据 T 的类型选择不同的线性插值函数
template <typename T>
T Interpolate(const T& p1, const T& p2, long targetTimestamp);

// 针对 FxyzInfo 类型的特化
template <>
inline FxyzInfo Interpolate(const FxyzInfo& p1, const FxyzInfo& p2, long targetTimestamp) {
    long t1 = p1.timestamp;
    long t2 = p2.timestamp;
    if (t1 == t2)
        return p1;
    double factor = static_cast<double>(targetTimestamp - t1) / (t2 - t1);
    FxyzInfo result;
    result.x = p1.x + (p2.x - p1.x) * factor;
    result.y = p1.y + (p2.y - p1.y) * factor;
    result.z = p1.z + (p2.z - p1.z) * factor;
    result.timestamp = targetTimestamp;
    return result;
}

// 针对 FrotInfo 类型的特化
template <>
inline FrotInfo Interpolate(const FrotInfo& q1, const FrotInfo& q2, long targetTimestamp) {
    long t1 = q1.timestamp;
    long t2 = q2.timestamp;
    if (t1 == t2)
        return q1;

    // 计算插值因子
    double factor = static_cast<double>(targetTimestamp - t1) / (t2 - t1);

    // 获取四元数的分量
    double q0_1 = q1.rot.getQ0();
    double q1_1 = q1.rot.getQ1();
    double q2_1 = q1.rot.getQ2();
    double q3_1 = q1.rot.getQ3();

    double q0_2 = q2.rot.getQ0();
    double q1_2 = q2.rot.getQ1();
    double q2_2 = q2.rot.getQ2();
    double q3_2 = q2.rot.getQ3();

    // 计算点积
    double dot = q0_1 * q0_2 + q1_1 * q1_2 + q2_1 * q2_2 + q3_1 * q3_2;

    // 如果点积为负，取反一个四元数以确保最短路径
    if (dot < 0.0) {
        q0_2 = -q0_2;
        q1_2 = -q1_2;
        q2_2 = -q2_2;
        q3_2 = -q3_2;
        dot = -dot;
    }
    double q0_, q1_, q2_, q3_;
    // 避免数值不稳定
    const double DOT_THRESHOLD = 0.9995;
    if (dot > DOT_THRESHOLD) {
        // 当点积接近 1 时，使用线性插值
        q0_ = q0_1 + factor * (q0_2 - q0_1);
        q1_ = q1_1 + factor * (q1_2 - q1_1);
        q2_ = q2_1 + factor * (q2_2 - q2_1);
        q3_ = q3_1 + factor * (q3_2 - q3_1);
    } else {
        // 使用球面线性插值（SLERP）
        double theta_0 = std::acos(dot);
        double theta = theta_0 * factor;
        double sin_theta = std::sin(theta);
        double sin_theta_0 = std::sin(theta_0);

        double s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
        double s1 = sin_theta / sin_theta_0;

        q0_ = s0 * q0_1 + s1 * q0_2;
        q1_ = s0 * q1_1 + s1 * q1_2;
        q2_ = s0 * q2_1 + s1 * q2_2;
        q3_ = s0 * q3_1 + s1 * q3_2;
    }

    // 创建插值后的 FrotInfo 对象
    FrotInfo result(targetTimestamp, q0_, q1_, q2_, q3_, true);
    return result;
}
class SyncedSensorInterpolatorAGM {
   public:
    SyncedSensorInterpolatorAGM(double freq, std::function<void(const InterpedDataPackAGM&)> cb);

    void PushAccData(const FxyzInfo& data);
    void PushGyroData(const FxyzInfo& data);
    void PushMagData(const FxyzInfo& data);

    InterpedDataPackAGM BatchLoadAndInterp(const InterpedDataPackAGM& inputs);

    template <typename T>
    bool IsTimestampInRange(long next_time_cursor, const std::deque<T>& seq) {
        return next_time_cursor >= seq.front().timestamp && next_time_cursor < seq.back().timestamp;
    }

    template <typename T>
    void RemoveExtraPrevs(std::deque<T>& seq) {
        while (seq.size() > 1 && seq[1].timestamp <= time_cursor) {
            seq.pop_front();
        }
    }

   private:
    template <typename T>
    std::vector<T> OffloadData(std::deque<T>& interped_data) {
        std::vector<T> result;
        for (size_t i = 0; i < offload_size; ++i) {
            result.push_back(interped_data.front());
            interped_data.pop_front();
        }
        return result;
    }

    template <typename T>
    void PushData(const T& data, std::deque<T>& sensorData) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!sensorData.empty() && data.timestamp < sensorData.back().timestamp) {
            // 如果接收数据时间戳小于最后一帧，则丢弃
            return;
        }
        sensorData.push_back(data);
        if (!IsCursorInited()) {
            bool sucess = InitCursor();
            if (!sucess) {
                return;
            }
        }
        while (CanInterp()) {
            Interp();
            RemoveExtraData();
        }
        Offload();
    }

    template <typename T>
    void InterpData(const std::deque<T>& seq, std::deque<T>& interped_seq) {
        size_t index = 0;
        // 找到第一个满足条件的元素的索引
        for (; index < seq.size(); ++index) {
            if (seq[index].timestamp > time_cursor) {
                break;
            }
        }

        if (index == seq.size()) {
            throw std::runtime_error("cannot find next for interp");
        }

        if (index == 0) {
            throw std::runtime_error("cannot find prev for interp");
        }
        T interp_data = Interpolate<T>(seq[index - 1], seq[index], time_cursor);
        interped_seq.push_back(interp_data);
    }

    std::mutex mutex_;
    bool IsCursorInited();
    bool InitCursor();
    bool CanInterp();
    void RemoveExtraData();
    void Interp();
    void Offload();
    double sampling_frequency;
    long sampling_peroid_ms = 10;
    long time_cursor = -1;
    size_t offload_size = 20;

    std::deque<FxyzInfo> accs;
    std::deque<FxyzInfo> gyros;
    std::deque<FxyzInfo> mags;

    std::deque<FxyzInfo> interped_accs;
    std::deque<FxyzInfo> interped_gyros;
    std::deque<FxyzInfo> interped_mags;

    std::function<void(const InterpedDataPackAGM&)> callback;
};

}  // namespace XDR

#endif  // XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_AG_H_
