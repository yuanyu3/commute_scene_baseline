/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Cellular provider.
 */

#ifndef CELL_PROVIDER_H
#define CELL_PROVIDER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "SignalTypes.h"
#include "cell_information.h"

namespace OHOS::Multimedia::CameraAgentService {

class CellProvider final {
public:
    using CellProviderListener = std::function<void(const std::vector<std::shared_ptr<CellInfo>> &cellVec)>;
    static CellProvider *GetInstance();
    bool RegisterListener(const CellProviderListener &listener);
    bool UnRegisterListener();
    void Enable();
    void Disable();
    /** Poll period; takes effect on the next sleep in the trigger loop. */
    void SetScanIntervalMs(int64_t intervalMs);
    int64_t GetScanIntervalMs() const;

private:
    CellProvider() = default;
    ~CellProvider() = default;
    void Operation();
    std::vector<std::shared_ptr<CellInfo>> GetCellInfo(int32_t slotId);
    std::shared_ptr<CellInfo> ParseNr(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);
    std::shared_ptr<CellInfo> ParseLte(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);
    std::shared_ptr<CellInfo> ParseTdsCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);
    std::shared_ptr<CellInfo> ParseWCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);
    std::shared_ptr<CellInfo> ParseCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);
    std::shared_ptr<CellInfo> ParseGsm(sptr<OHOS::Telephony::CellInformation> cell, int64_t timestamp);

    static constexpr int64_t DEFAULT_TRIGGER_INTERVAL_MS = 1500;
    std::mutex mutex_;
    bool running_ { false };
    std::atomic<int64_t> scanIntervalMs_ { DEFAULT_TRIGGER_INTERVAL_MS };
    CellProviderListener listener_ { nullptr };
    std::thread triggerThread_;
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // CELL_PROVIDER_H
