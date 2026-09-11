# 坐标系统一（WGS84）

**本仓库独立：`sa_service` Ability dump 与 `sa_cpp` 均写 WGS84，不依赖 helloworld。**

> 标准名称是 **WGS84**（不是 WGS64）。

## 约定

| 数据 | CRS |
|------|-----|
| `location_data_*.csv`（本库） | **WGS84** + `coordinate_system` 列 |
| `sensor_events` GPS payload | **WGS84** |
| 锚点 / 状态机 / θ / 回放输出 | **WGS84** |
| 国内地图选点 | 视为 GCJ，用 `Gcj02ToWgs84` / Python `crs.gcj02_to_wgs84` 后入库 |

## 代码位置

| 语言 | 路径 |
|------|------|
| C++ | `sa_cpp/include/commute_sa/crs.h` |
| Python | `python/commute_baseline/crs.py` |
| Dump | `sa_cpp` → `EnqueueLocationWgs84` / `OnGpsLocation` |

## 历史 GCJ dump（可选兼容）

旧 helloworld 包若仍是 GCJ：

```bash
python python/scripts/convert_location_to_wgs84.py --raw-dir <gcj> --out-dir data/location_wgs84
# 或
python python/scripts/replay_baseline.py --raw-dir <gcj> --location-crs GCJ02
```

新采集不要再写 GCJ。

`source_type` 不改变坐标系，也不参与在线围栏或外向证据计算。回放旧数据时必须先按
`--location-crs` 转换，再计算距离与 GPS 可靠度，不能用 `source_type` 猜测 CRS。
