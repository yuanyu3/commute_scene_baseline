//
// Created by z00838343 on 2025/2/25.
//

#ifndef MAPDEMO_LEAVE_CAR_DETECTOR_H
#define MAPDEMO_LEAVE_CAR_DETECTOR_H

#include <deque>
#include <vector>
#include "common.h"

namespace XDR {

struct ShakingPeroid {
    long start_time;
    long end_time;
};

class LeaveCarDetector {
public:
    LeaveCarDetector();
    ~LeaveCarDetector() = default;
    // 单拍加载数据并处理
    bool PushAccGyroAndInferIsLeaveCar(const FxyzInfo& acc, const FxyzInfo& gyro, const FxyzInfo& mag);
    long GetLeaveCarTimestamp();
    long GetStartWalkTimestamp();
    bool GetIsLeaveCar();

   private:
    void PushAccGyro(const FxyzInfo& acc, const FxyzInfo& gyro, const FxyzInfo& mag);
    void UpdateState();
    void InferLeaveCarTimestamp();
    // void infer
    std::pair<bool, bool> InferIsShakingStateByWindowGyro(const std::vector<std::vector<double>>& gyro_window);
    bool InferIsWalkingStateByWindowAcc(const std::vector<std::vector<double>>& acc_window);
    bool InferIsOutsideByWindowMag(const std::vector<std::vector<double>>& mag_window);

    std::deque<std::vector<double>> acc_buffer_;
    std::deque<std::vector<double>> gyro_buffer_;
    std::deque<std::vector<double>> mag_buffer_;

    std::deque<long> timestamps_;
    std::deque<bool> is_walking_list_;
    std::deque<bool> is_possibly_leave_car_;

    std::vector<ShakingPeroid> shaking_periods_;
    int acc_cursor_;
    int gyro_cursor_;

    bool is_leave_car_ = false;
    long leave_car_timestamp_ = 0;
    long start_walk_timestamp_ = 0;

    double acc_filter_sigma_ = 2;  // 离车检测前acc滤波标准差
    double acc_filter_truncate_ = 4.0;

    int std_wsize_ = 400;                   // 利用std检测手机晃动的窗口长度
    double std_shaking_threhold_ = 0.3;     // 用于检测手机晃动的标准差阈值
    double std_walking_threhold_ = 0.2;     // 用于检测手机长时解挂载的标准差阈值
    int fft_wsize = 1024;                   // 对acc做fft的窗口长度
    double fft_threhold1_ = 400;            // 寻找频域有步行特征频域信号幅值的阈值
    double fft_threhold2_ = 150;            // 寻找频域有步行特征频域信号幅值的阈值
    double amplitude_diff_threhold_ = 0.1;  // 主次信号占比差阈值
    double amplitude_main_threhold_ = 0.1;  // 主信号占比阈值
    double freq_lowest_ = 0.5;              // 步行信号的下限频率
    double freq_highest_ = 10;              // 步行信号的上限频率
    int sensor_rate_ = 100;                 // 信号频率
    int merge_sec_ = 15;                    // 最终判断真正离车时刻，多个滑窗合并的时间阈值（s）
    int drop_sec_ = 15;                     // 最终判断真正利车时刻，向前丢弃长时间静止的时段阈值（s）
    int sliding_mean_wsize_ = 20;           // 滑窗均值滤波，窗口20
    int step = 128;                         // fft滑窗步长
    int mag_threhold_ = 50;                 // mag判断车外阈值
    bool retry_find_walk = false;
};

}  // namespace XDR

#endif  // MAPDEMO_LEAVE_CAR_DETECTOR_H
