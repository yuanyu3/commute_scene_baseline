/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr pdr provider.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_H_
#define XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_H_

#include <vector>
#include <deque>
#include <complex>
#include <memory>
#include "riemann_pdr_common.h"
#include "riemann_pdr_sensor_interpolator.h"
#include "riemann_pdr_rotation.h"
#include "riemann_pdr_model.h"
#include "riemann_pdr_pdr_result.h"
#include "riemann_pdr_vector3d.h"

namespace AIPDR {
namespace XDR {
constexpr double XDR_DT_WINDOW = 0.1;

enum class MagCalibStrategy { DISABLE, BY_MAG, BY_RV, BY_RV_MAG_MIXED };

enum class PoseInitStrategy { DISABLE, BY_INIT_ACC, BY_INIT_GRV };

class PdrStepRotationProvider {
public:
    static PdrStepRotationProvider& GetInstance() {
                static PdrStepRotationProvider instance(
            [](const FxyzInfo&) { /* 空回调，按需可扩展 */ },
            {SensorType::ACC, SensorType::GYRO, SensorType::MAG, SensorType::RV}, // 启用 RV！
            false // useNpu
        );
        return instance;
    }

    // 禁止拷贝
    PdrStepRotationProvider(const PdrStepRotationProvider&) = delete;
    PdrStepRotationProvider& operator=(const PdrStepRotationProvider&) = delete;
    
    explicit PdrStepRotationProvider(std::function<void(const FxyzInfo &)> onResultCallback,
        const std::vector<SensorType> &sensors = {SensorType::ACC, SensorType::GYRO, SensorType::MAG},
        bool useNpu = false);
    void UpdateAcc(const FxyzInfo &acc);
    void UpdateGyro(const FxyzInfo &gyro);
    void UpdateMag(const FxyzInfo &mag);
    void UpdateGrv(const FrotInfo &grv);
    void UpdateRv(const FrotInfo &rv);
    void BatchLoadAndInfer(const InterpedDataPack &fullDataPack);
    void SetQwiCurr(const Rotation &q);
    void SetUseRiemannGrv(bool flag);
    void SetMagCalibStrategy(MagCalibStrategy strategy);
    void SetGrvCallback(std::function<void(const Rotation &)> callback);
    void SetOnPdrResultCallback(std::function<void(const PdrResult &)> cb);
    void SetPoseInitStrategy(PoseInitStrategy strategy);
    static std::vector<double> Rotate2DVector(const std::vector<double> &vec, double angle);
    void SetModelRunFunc(std::function<void()> cb);
    void GetModelInput(float *input, int size);
    void SetModelOutput(int res, float x, float y);
    void ResetCache();

protected:
    // 创建pdr结果
    PdrResult CreatePdrResult();
    bool isUseStepMag = true;
    std::vector<PdrResult> pdrResults;
    std::vector<double> velocity;

private:
    void ProcessInterpedDataPack(const InterpedDataPack &data_pack);
    std::vector<Rotation> RiemannGrv(const std::vector<FxyzInfo> &accs, const std::vector<FxyzInfo> &gyros);
    std::vector<Rotation> PhoneGrv(const std::vector<FrotInfo> &grvs) const;
    void InitPoseByAcc(const FxyzInfo &first_acc);
    void InitPoseByGrv(const FrotInfo &grv);
    void ProcessWindow(const InterpedDataPack &data_pack);
    void ProcessAgyroQueue(const std::vector<FxyzInfo> &accs, const std::vector<FxyzInfo> &gyros,
        const std::vector<Rotation> &q_wi_corr_window);
    void ProcessMag(
        const std::vector<FxyzInfo> &mags, size_t window_size, const std::vector<Rotation> &q_wi_corr_window);
    std::vector<Rotation> ProcessGrv(const InterpedDataPack &data_pack);
    void ConsRawDatum();
    std::vector<double> RunModelAndGetOutput();

    void UpdateThetaQueueByMag(const std::vector<Vector3D> &amags);
    void UpdateThetaQueueByRv(const std::vector<Rotation> &q_wi_corrs, const std::vector<FrotInfo> &rvs);

    void UpdateMagCalibratedTheta();
    void UpdateRvCalibratedTheta();
    double ChooseThetaByStrategy();
    double CalcTheasCan(std::vector<double> &thetas_stable_can, const std::vector<double> thetas_stable) const;

    // 实用函数
    Rotation IntegralDeltaQ(const FxyzInfo &gyro) const;

    // 传感器类型检查
    bool CheckSensorType();

    // 插值器
    RiemannPdrSensorInterpolator sensor_interpolator;

    // 数据buffer
    size_t acg_window_size = 200;
    size_t gacc_window_size = 200;
    size_t mag_calib_window_size = 200;

    std::deque<FxyzInfo> aacc_queue;   // 窗口200，用于模型推理
    std::deque<FxyzInfo> agyro_queue;  // 窗口200，用于模型推理
    std::deque<FxyzInfo> gacc_queue;   // 窗口200，用于对齐重力轴

    // std::deque<FxyzInfo> amag_queue;  // 窗口200，用于求磁偏角
    std::deque<bool> is_mag_amplitude_stable_queue;  // 窗口200，用于过滤磁信号
    std::deque<double> mag_theta_queue; // 窗口200，用于求磁偏角
    std::deque<double> rv_theta_queue; // 窗口200，用于求磁偏角

    std::deque<double> calib_mag_theta_queue;
    std::deque<double> calib_rv_theta_queue;
    size_t rv_mag_mix_window_size = 50;

    bool is_mix_theta_inited = false;
    double mix_theta_last = 0.0;

    // 模型
    std::unique_ptr<Model> pdr_model;

    double dt = 0.005;

    Rotation q_wi_curr = Rotation(1.0, 0.0, 0.0, 0.0, true);

    // pdr单帧结果计算完成触发回调
    std::function<void(const FxyzInfo &)> on_result_callback;
    std::function<void(const Rotation &)> on_grv_callback;

    bool is_use_riemann_grv = true;

    // pose初始化
    bool is_pose_inited = false;

    // 磁校准
    MagCalibStrategy mag_calib_strategy = MagCalibStrategy::BY_MAG;
    double current_theta_mag = 0.0;
    double current_theta_rv = 0.0;
    double mag_e = -2965.679608634033 / 1000;
    double mag_n = 33519.23756210379 / 1000;
    double mag_stable_amplitude_lower_bound = 30.0;
    double mag_stable_amplitude_upper_bound = 60.0;
    size_t mag_stable_cal_std_threshold = 20;

    // grv初始化
    PoseInitStrategy pose_init_strategy = PoseInitStrategy::BY_INIT_ACC;

    std::vector<SensorType> sensors;
    bool have_checked_sensor_type = false;

    // grv最新帧记录
    FrotInfo q_wi_corr_latest;
    std::function<void(const PdrResult &)> on_pdr_result_callback;

    // 此次pdr结果时间记录
    long pdr_result_timestamp = -1;
};

}  // namespace XDR
}

#endif  // XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_H_
