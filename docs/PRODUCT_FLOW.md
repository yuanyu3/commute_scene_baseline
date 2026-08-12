# 产品流程（非 helloworld）

```text
1) 采集        Ability 硬件 dump（WGS84）+ 离开窗口稀疏样本
2) 标准化判定  每 tick → SceneEngine（无 LLM）→ 预测推送（仍在家 + ETA≤lead_max）
3) 个性化      PersonalizationController 触发时 → Agent 生成候选策略/参数
4) 验证部署    语义历史反事实回放 → policy.json 原子提交或回滚
```

推送约束：`OUTSIDE` 不发「带钥匙」；同一离开 episode 只推一次；首次 `OUTSIDE` 记 `t*`/`lead_s` 并立刻 `CONFIRMED_LEAVE` 改参；若约 20min 内从未 `OUTSIDE` 则 `FALSE_PUSH` 再改参。

## LLM 何时调用（流程门控，不是 env 开关）

| 时机 | 行为 |
|------|------|
| 平时每个 tick | **不调** LLM |
| SceneEngine `should_service` | 本地推送，仍不调 LLM |
| 推送后首次 `OUTSIDE`（`CONFIRMED_LEAVE`） | **立即调用** θ 个性化 LLM |
| 推送后约 20min 仍未 `OUTSIDE`（`FALSE_PUSH`） | **调用** θ 个性化 LLM |
| 本地 ≥22 点且当日未跑（`DAY_END`） | **调用** θ 个性化 LLM |

`agent.env` 只放 **API 凭证** 与可选 `SA_AGENT_DEBUG_SINKS`，**不**用 `SA_AGENT_INVOKE` / `SA_AGENT_PERSONALIZE` 控制是否调用。

## 落盘（最小集，默认）

根目录：`/data/service/el1/public/commuteagentservice/`

| 文件 | 用途 |
|------|------|
| `anchors.json` | 家/公司锚点（WGS84） |
| `theta.json` | SceneEngine 参数 θ |
| `policy.json` | 端侧策略解释器当前生效的高层策略 |
| `policy_history.jsonl` | 日终反事实评估所需的标注语义样本 |
| `leave_episodes.jsonl` | 推送记录 + settle 标签（CONFIRMED_LEAVE / FALSE_PUSH） |
| `leave_window_samples.jsonl` | 推送后离开窗口内稀疏 GPS/行走 |
| `param_changes.jsonl` | θ 变更审计 |
| `personalize_jobs.jsonl` | 改参任务 |
| `audit.jsonl` | Agent write_audit（含 no_op） |
| `anchor_reestimate_jobs.jsonl` | 锚点重估队列 |

默认**不写**每 tick 的 `sensor_events` / `sa_perception_ticks` / `semantic_snapshots` / `baseline_decisions`。  
需要排查时设 `SA_AGENT_DEBUG_SINKS=1`。
