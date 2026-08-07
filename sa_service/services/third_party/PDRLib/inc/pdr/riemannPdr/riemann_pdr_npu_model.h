/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_INCLUDE_NPU_MODEL_H_
#define XDR_INCLUDE_NPU_MODEL_H_

#include <functional>
#include "riemann_pdr_model.h"

namespace AIPDR {
namespace XDR {

class NpuModel : public Model {
public:
    NpuModel() : Model(){};
    ~NpuModel() override = default;
    void Run() override;
    void SetRunFunc(std::function<void()> cb) override;
    void GetModelInput(float *input, int size) override;
    void SetModelOutput(int res, float x, float y) override;
};

}  // namespace XDR
}

#endif  // XDR_INCLUDE_NPU_MODEL_H_
