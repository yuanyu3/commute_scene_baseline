# SA C++ 采集库（本仓库自有，不依赖 helloworld_agent）

从 helloworld 抽离并固化的 **dump + CRS** 代码。本仓库是唯一维护方；接入时把 `sa_cpp` 编进你的 SA / Ability，**不要**再链 helloworld。

## 与 helloworld 的差异（刻意）

| 项 | helloworld | 本库 |
|----|------------|------|
| `location_data_*.csv` | GCJ-02 | **WGS84** + `coordinate_system` 列 |
| `sensor_events` GPS | WGS84 | WGS84（同） |
| 依赖 | Ability / OHOS 全量 | 仅标准 C++17 |

## 模块

| 头文件 | 作用 |
|--------|------|
| `commute_sa/crs.h` | GCJ↔WGS84（地图锚点入库用） |
| `commute_sa/types.h` | `RawGpsLocation` / 事件类型 |
| `commute_sa/rolling_sensor_dump.h` | 滚动 CSV：location/IMU/wifi/ble/cell |
| `commute_sa/sensor_events_writer.h` | `sensor_events.csv` |

## 接入（伪代码）

```cpp
#include "commute_sa/rolling_sensor_dump.h"
#include "commute_sa/sensor_events_writer.h"

commute_sa::RollingSensorDump dump("/data/.../commute_scene");
commute_sa::SensorEventsWriter events("/data/.../commute_scene/sa_sensor");
dump.Start();
events.Start();

// OnLocationReport: OHOS 给的 lat/lon 已是 WGS84 → 直接 dump，禁止转 GCJ
void OnLocationReport(double wgsLat, double wgsLon, double acc, int sourceType, int64_t tsMs) {
    dump.EnqueueLocationWgs84(tsMs, wgsLat, wgsLon, acc, sourceType);

    commute_sa::RawGpsLocation loc;
    loc.latitude = wgsLat;
    loc.longitude = wgsLon;
    loc.horizontal_accuracy_m = acc;
    loc.has_horizontal_accuracy = true;
    loc.valid = true;
    loc.source_type = sourceType;
    loc.observed_at = tsMs;
    events.OnGpsLocation(loc);
}
```

国内地图选点（GCJ）→ 锚点：

```cpp
auto home = commute_sa::Gcj02ToWgs84(mapLat, mapLon);
// 写入 anchors.json / 围栏，一律 WGS84
```

## 编译

```bash
cd sa_cpp
cmake -B build -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build build
./build/commute_sa_smoke   # 或 build\Debug\commute_sa_smoke.exe
```

把 `commute_sa` 静态库链入你的 SA 工程即可。

## 输出目录约定

```text
<dumpRoot>/<YYYYMMDD_HHMMSS>/
  location_data_*.csv     # WGS84
  CRS.txt
  acc_data_*.csv / ...
  wifi_data_*.csv / ble_data_*.csv / cell_data_*.csv

<eventsRoot>/<YYYYMMDD_HHMMSS>/
  sensor_events.csv       # GPS payload coordinate_system=WGS84
  CRS.txt
```

Python 回放默认读 WGS84：`--location-crs WGS84`。
