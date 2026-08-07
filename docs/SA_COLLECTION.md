# SA 数据采集（`sa_service`）

完整工程在 **`sa_service/`**（从 helloworld 同步后独立维护）。  
轻量可单测 dump 在 `sa_cpp/`。

## 模块

| 路径 | 用途 |
|------|------|
| `sa_service/services/src/ability/AgentServiceAbility.cpp` | 订阅、DumpWorker、`OnLocationReport` |
| `sa_service/services/src/proactive/ProactiveAgentBusinessModule.cpp` | SceneEngine tick；产品最小集落盘；可选 DEBUG_SINKS |
| `sa_service/services/src/engine/{Pdr,LeaveCar,Geo}Engine.cpp` | PDR / 步行 / 逆地理 |
| `sa_service/services/src/provider/*` | WiFi / BLE / CELL |
| `sa_service/etc/init/commuteagentservice.cfg` | 进程与权限 |
| `sa_service/sa_profile/9903.json` | SA 注册 |

## 必须采集的流

| 流 | 事件/文件 | 用途 |
|----|-----------|------|
| GPS | `GPS_REPORT` + `location_data_*.csv` | 锚点、通勤（**WGS84**） |
| Motion | `WALKING_STARTED/STOPPED` | LEAVING 预兆 |
| PDR | `PDR_POINT` / 轨迹 CSV | 净外向 |
| WiFi / BLE / CELL | `*_data_*.csv` | 指纹 / 室内 |
| Tick | SceneEngine 内存态；DEBUG 时才写 `sa_perception_ticks.csv` | 场景判定 / 回放 |

## 坐标系

`OnLocationReport` → dump / `gLocationFrame` / PDR / `OnGpsLocation`：**一律 WGS84**。  
GeoEngine 内部可另存 GCJ 仅供地图显示。

## 输出目录

```text
/data/service/el1/public/commuteagentservice/
  anchors.json, theta.json
  leave_episodes.jsonl, leave_window_samples.jsonl
  param_changes.jsonl, personalize_jobs.jsonl
  <session>/                 # Ability 硬件 dump（采集会话）
    location_data_*.csv      # WGS84
    CRS.txt
    acc_/gyro_/... / wifi_/ble_/cell_*.csv

/data/service/el1/public/commuteagentservice/sa_sensor_test/<run>/   # 仅 SA_AGENT_DEBUG_SINKS=1
  sensor_events.csv
  sa_perception_ticks.csv
  ...
```

## 编译

见 [`sa_service/README.md`](../sa_service/README.md)。无 OH 树 / 缺权限时可不编译。
