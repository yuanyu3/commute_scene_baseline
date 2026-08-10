# Radio leave evidence（HSMM 观测）

权威实现：`sa_cpp` SceneEngine + RadioEvidence；主机回放：`python/commute_baseline/engine.py`。

## 判断逻辑

各模态先转换为 `[0,1]` 语义观测，再由 HSMM 结合历史状态、显式持续时间和当前位置关系计算 `P(LEAVING)`：

```text
s_walk, s_pdr, s_geo, s_wifi, s_cell, s_ble, s_time
observation_likelihood = P(s_* | hidden_state)
p_leaving = HSMM(observation_likelihood, duration, previous_posterior)
hits = count(s_i ≥ thr_i)  # 仅作推送安全门控

leave_cand =
  INSIDE|NEAR
  AND NOT approaching / wifi_attach
  AND p_leaving ≥ enter_leave
  AND hits  ≥ min_evidence
  AND arm_ok
```

| 通道 | 语义观测 | 默认可靠度参数 | 默认 hit 阈值 |
|------|----------|----------|----------------|
| walk | 步行=1 | `w_walk` 0.25 | `thr_walk` 0.5 |
| pdr | 净外向位移归一化 | `w_pdr` 0.20 | `thr_pdr` 0.5 |
| geo | **仅距离上升**时 `dist/r_out` | `w_geo` 0.20 | `thr_geo` 0.5 |
| wifi | `1−Jaccard`（≤`thr_wifi_jaccard`→1） | `w_wifi` 0.12 | `thr_wifi` 0.5 |
| cell | 切站=1 | `w_cell` 0.08 | `thr_cell` 0.5 |
| ble | churn=1 | `w_ble` 0.02 | `thr_ble` 0.5 |
| time | 离开时刻 prior | `w_time` 0.20 | `thr_time` 0.5 |

`w_*` 现在控制观测似然的可靠度，不再直接求和。可调 `enter_leave`、`min_evidence`、各 `w_*` / `thr_*`（见 `config/theta_default.json`）。

**不是** `rising OR wifi_detach` 这类布尔 OR 门控，也不是单 tick 加权总分。

## 硬门控（模型外）

仅用于回程/已在外，避免「回公司当离开」：

- `APPROACHING` / `wifi_*_attach` → 强化 `AT_ANCHOR` 似然且禁推
- `OUTSIDE→NEAR/INSIDE` 边沿
- 回程结束后 `radio_suppress_after_approach_s`（默认 120s）内 **radio 离开分置 0**（防 soft 漂移误推）

## WiFi / CELL 压缩

见 RadioEvidence：soft dwell set、Jaccard detach/attach、cell 稳站后切站、BLE temporal churn。

## 主机验证

```bash
python examples/run_leave_company_batch.py --sensor-root D:/huawei/data/sa_sensor_test --iters 1 \
  --out-root output/leave_company_score_thr --sessions 20260810_114956
```

推送 evidence 中可见 `s_wifi` / `s_cell` / `channels`。
