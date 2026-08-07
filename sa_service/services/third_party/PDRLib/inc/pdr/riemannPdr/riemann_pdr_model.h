/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_MODEL_H_
#define XDR_XDR_INCLUDE_MODEL_H_

#include <array>
#include <functional>


namespace AIPDR {
constexpr int XDR_WINDOW_LEN = 200;
constexpr int XDR_PDR_INPUT = 1200;  // 1 * 6 * 200 * 1
constexpr int XDR_PDR_OUTPUT = 2;    // 1 * 2
namespace XDR {

class Model {
public:
    Model() = default;
    virtual ~Model() = default;
    virtual void Run() = 0;
    // pdr model
    std::array<float, XDR_PDR_INPUT> m_inputFeature{};
    std::array<float, XDR_PDR_OUTPUT> m_results{};
    virtual void SetRunFunc(std::function<void()> cb){};
    virtual void GetModelInput(float *input, int size){};
    virtual void SetModelOutput(int res, float x, float y){};

protected:
    std::function<void()> m_callFunc;
};

}  // namespace XDR
}

#endif  // XDR_XDR_INCLUDE_MODEL_H_
