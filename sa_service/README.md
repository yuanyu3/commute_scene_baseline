# commuteagentservice（本仓库 SA 工程）

从 `helloworld_agent` 同步的系统 SA 工程，归属 **commute_scene_baseline**，可脱离 helloworld 维护。

| 项 | 值 |
|----|-----|
| 组件名 | `commuteagentservice` |
| SA ID | **9903**（避免与 helloworld 9902 冲突） |
| 共享库 | `libcommuteagentservice.z.so` |
| Dump 根 | `/data/service/el1/public/commuteagentservice/` |
| sensor_events | `.../commuteagentservice/sa_sensor_test/<run>/` |
| agent.env | `/data/service/el2/9903/agent.env` |
| location CSV CRS | **WGS84**（含 `coordinate_system` 列 + `CRS.txt`） |

## 目录

```text
sa_service/
  BUILD.gn / bundle.json
  etc/init/commuteagentservice.cfg   # 权限与进程
  etc/agent.env.example
  sa_profile/9903.json
  services/                          # Ability + dump + PDR/LeaveCar + ProactiveAgent
  interfaces/napi|declaration        # 可选 HAP 客户端
```

## 相对 helloworld 的关键改动

1. **location dump 写 WGS84**，不再把 GCJ 写入 `location_data_*.csv`
2. 内存 `gLocationFrame` 改为 WGS84（与 PDR / `OnGpsLocation` 一致）
3. 组件 / 路径 / SA ID 重命名为 commuteagentservice / 9903
4. GeoEngine 仍可内部用 GCJ 做地图显示，不影响 dump

## 编译说明（需 OpenHarmony 源码树）

本目录**不能**在普通 Windows 桌面用 MSVC/MinGW 完整编过——依赖 `//build/ohos.gni`、sensor/location/safwk、`jiuwen_core`、设备预置 `libPDR.so` / `libleave_car.so`。

推荐：把本目录挂到 OH 树，例如：

```text
//foundation/multimedia/commute_scene_baseline/sa_service
```

与 `bundle.json` 中 `sub_component` 路径一致，再在产品 `parts` 里打开 `commuteagentservice`。

缺权限 / 缺 SDK / 无设备预置库时，**允许不编译**；源码与配置仍作为本仓库的 SA 工程真相来源。主机侧算法回放继续用仓库根目录的 `python/`。

可选：独立轻量 dump 库见上层 `sa_cpp/`（不依赖 OHOS，可单测）。

## 端上：SceneEngine 已接 SA tick

详见 [`docs/ON_DEVICE_TICK.md`](../docs/ON_DEVICE_TICK.md) / [`docs/PRODUCT_FLOW.md`](../docs/PRODUCT_FLOW.md)。

每 tick：`SceneEngine` 判定与推送（无 LLM）。  
改参：仅 `AFTER_PUSH` / `DAY_END` 时调在线 LLM。`agent.env` 只配凭证，不控调用节奏。

## 运行时采集

IPC 启动（NAPI 或同进程调用）：

- `StartSensorCollection` → IMU dump + LeaveCar + ProactiveAgent
- `StartLocationCollection` → GPS dump（WGS84）+ PDR GPS + `OnGpsLocation`
- `StartWifi|Ble|CellCollection` → radio dump
