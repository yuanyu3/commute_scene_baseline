# PDR leave evidence

权威实现：`sa_cpp/include/commute_sa/pdr_evidence.h` → `BaselineRuntime` → `SceneEngine::BuildLeaveObservation` → `LeaveHsmm`。

来自 `20260804_175841_sensor` / 基线算法：步行 episode 的 **净位移（米）** 作为「向外走了多远」的辅证；不是到家/公司锚点的 GPS 距离。

## 特征

| 量 | 含义 |
|----|------|
| `net_displacement_m` | 本段步行起点 → 当前 PDR `(x,y)` 直线距离 |
| `path_length_m` | 轨迹折线长 |
| `straightness` | `net / path`；室内来回晃时压低 credit |
| `pdr_net_out_home_m` / `_company_m` | 归因后的净外向米数，写入 `TickFeatures` |

## 归因

1. `OnWalkingStarted` 开 episode，清零原点  
2. `OnPdrPoint(x,y)` 更新净位移 / 路径（本地平面米，与 SA dump 一致）  
3. 首次 GPS 为 `INSIDE`/`NEAR` 时 `NoteWalkContext` 打上 home 或 company 标签（本段粘住）  
4. 无 GPS 时临时两侧都填同一 net（靠 `focus_side` + HSMM/产品门控）
5. `OnWalkingStopped` 后不再给 leave credit（`NOT_WALKING`）

## HSMM 观测

```text
sPdr = clip01( pdr_net_out / max(15, r_in × 0.3) )
pdr_outbound = sPdr
evidence_strength_pdr = 0.85 # 直接乘发射对数似然；0表示完全忽略
hit   += 1  if sPdr ≥ 0.5      # 约 ≥7.5 m 或 0.15×r_in
```

ETA：GPS 速度不可用时，若 `pdr_net_out ≥ 8` 且 walking → 假定步行速度估出 `r_out`。

室内绕圈：`path ≥ 20m` 且 `straightness < 0.25` 时按比例压低 credit（对齐「路径涨、净位移小不算离家」）。

## SA 接线

1. `OnWalkingStarted/Stopped` → `BaselineRuntime`（已有）+ `PdrEvidence`  
2. `OnPdrPoint` → `BaselineRuntime::OnPdrPoint`  
3. 每 tick `OnTick` → `Evaluate()` → `feat.pdr_net_out_*`

## 主机验证

```bash
cd sa_cpp
cmake -B build -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build build
./build/commute_pdr_smoke
```
