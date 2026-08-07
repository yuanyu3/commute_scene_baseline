/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: WiFi scan provider.
 */

#ifndef WIFI_PROVIDER_H
#define WIFI_PROVIDER_H

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "SignalTypes.h"
#include "kits/c/wifi_device.h"
#include "wifi_scan.h"

namespace OHOS::Multimedia::CameraAgentService {

class WifiProvider {
public:
    using WifiProviderListener = std::function<void(WifiErrorCode, WifiFp &)>;
    static WifiProvider *GetInstance();
    bool RegisterListener(const WifiProviderListener &listener);
    bool UnRegisterListener();
    void Enable();
    void Disable();
    /** Scan period; takes effect on the next sleep in the trigger loop. */
    void SetScanIntervalMs(int64_t intervalMs);
    int64_t GetScanIntervalMs() const;

private:
    WifiProvider() = default;
    ~WifiProvider() = default;
    static void OnWifiScanStateChanged(int32_t state, int32_t size);
    void OnWifiReceived(WifiFp wifiFp);

    static constexpr int64_t DEFAULT_TRIGGER_INTERVAL_MS = 1500;
    std::mutex mutex_;
    bool running_ { false };
    std::atomic<int64_t> scanIntervalMs_ { DEFAULT_TRIGGER_INTERVAL_MS };
    WifiProviderListener listener_ {};
    WifiEvent wifiScanEventCallback_ = { 0 };
    std::thread triggerThread_;
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // WIFI_PROVIDER_H
