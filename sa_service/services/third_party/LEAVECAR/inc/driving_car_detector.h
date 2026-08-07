//
// Created by w30070423 on 2025/5/8.
//

#ifndef MAPDEMO_DRIVING_CAR_DETECTOR_H
#define MAPDEMO_DRIVING_CAR_DETECTOR_H

#include <deque>
#include <vector>
#include "common.h"
#include "rotation.h"

using namespace XDR;
// namespace XDR {

struct ShakingPeroid {
    long start_time;
    long end_time;
};

struct TrajSets {
    std::string datasetRoot;
    std::string scene;
};

struct DrivingCarConfig {
    std::string output_dir;
    TrajSets trag_sets;
};

class RiemannGrv {
public:
    RiemannGrv() = default;
    std::vector<double> PushDataAndGetWacc(long tmsp, const std::vector<double>& acc, const std::vector<double>& gyro);
private:
    void InitPoseByAcc(const FxyzInfo& first_acc);
    size_t gacc_window_size = 200;
    long prev_tmsp = -1;
    Rotation q_wi_curr = Rotation(1.0, 0.0, 0.0, 0.0, true);
    bool is_pose_inited = false;
    std::deque<FxyzInfo> gacc_queue;
    std::vector<double> prev_gyro;
};

class DrivingCarDetector {
public:
    DrivingCarDetector(bool is_use_grv=true);
    ~DrivingCarDetector() = default;
    void load_initial_state();
    void pushSensor(long tmsp, const std::vector<double>& acc, const std::vector<double>& gyro);
    void drivingCarDetectionOnline(const std::deque<long> tmsp, const std::deque<std::vector<double>> acc_w, const std::deque<std::vector<double>> gyro);
    bool pushAGMAndInferIsDriving(long tmsp, const std::vector<double>& acc, const std::vector<double>& gyro);
    long getDrivingTimestamp();
    void updateState();
    bool inferMovingState(const std::deque<std::vector<double>>& acc_window);
    bool inferSteadyState(const std::deque<std::vector<double>>& gyro_window);
    std::vector<std::vector<double>> slidingMeanFiltering(const std::deque<std::vector<double>>& data, const int& window_size);
private:
    int std_wsize = 1000;          // std窗口长度
    double gyro_threhold = 0.03;   // 用于检测手机晃动的标准差阈值
    double acc_threhold = 0.1;     // 用于检测运动的标准差阈值
    int acc_fileter_wsize = 100;   // acc滑动均值窗口长度
    int gyro_fileter_wsize = 100;  // gyro滑动均值窗口长度
    int sensor_rate = 100;         // 信号频率

    bool is_driving_car = false;
    bool is_moving = false;
    bool is_steady = false;
    long driving_car_tmsp = 0;
    std::deque<long> timestamp;
    std::deque<std::vector<double>> acc_buffer;
    std::deque<std::vector<double>> gyro_buffer;
    int sensor_cursor = 0;

    RiemannGrv grv_calculator;
    bool is_use_grv = true;
};

// } // namespace XDR

#endif  // MAPDEMO_DRIVING_CAR_DETECTOR_H
