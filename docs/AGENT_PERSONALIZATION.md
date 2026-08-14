# Jiuwen 策略个性化 Agent

## 定位

- **不做**每 tick 场景推理（由基线完成）。
- **只在**误差门控 / 日终 / 锚点重估时 `Invoke`。
- 使用 Jiuwen `AgentType::REACT` 或 `PLAN_EXECUTE`，`maxTurn≥3`，**启用 Tool**。
- 优先生成受约束策略并做反事实回放；策略结构不需重新编译 HAP。

## 高层策略 API

| Tool | 用途 |
|------|------|
| `get_personalization_policy` | 当前生效的 `policy.json` |
| `get_policy_catalog` | 端侧预编译模板与可覆盖字段边界 |
| `begin_policy_trial` | 快照当前策略 |
| `apply_policy_candidate` | 应用经校验的候选模板 |
| `evaluate_policy_on_history` | 用 `policy_history.jsonl` 语义样本做反事实评分 |
| `revert_policy_trial` / `commit_policy_trial` | 回滚或原子接受候选 |

内置模板：仅 `confirmed_leaving`。实时引擎用 HSMM `P(LEAVING)` 推送，忽略 policy 菜谱；Agent 不要发明硬门控目录。气压自动进入 HSMM，重要性由 `w_baro` 调节。

`policy_history.jsonl` 每行是已标注的候选时刻，包含 PRE_LEAVE/LEAVING 概率、walking、WiFi/Cell/BLE、PDR、有效 GPS 和 lead。端侧只缓冲最近 30 分钟语义 tick，episode settle 时落盘最近 10 分钟；评估器按连续 tick 重建证据持续时间。

## 与场景 Agent 分离

| | 场景基线 | 本改参 Agent |
|--|----------|--------------|
| 频率 | 每 tick | 天级或事件 |
| LLM | 否（默认可关） | 是 |
| Tool | 无 | 必须 |
| 输出 | scene + 推送 | Δθ + audit |

参照：`bbpjiuwen-linux` 的 `Agent` / `RegisterTool` / `examples/hello_jiuwen`。

## Tools

### 证据（已注册）

| Tool | 入参 | 出参 |
|------|------|------|
| `get_theta` | — | 当前 θ JSON |
| `get_anchors` | — | home/company 锚点 |
| `get_error_stats` | `since_ms`, `scene` | n_push / false_push / confirmed_leave |
| `get_leave_episode` | `t_push_ms?` | push+label 行 |
| `get_leave_window_samples` | `t_push_ms?`, `limit?` | 稀疏 GPS/行走 |
| `get_leave_sensor_summary` | `t_push_ms?`, `before_s`, `after_s` | WiFi/CELL/GPS/**PDR**/磁**语义摘要**（非 raw CSV） |

实现：`sa_agent::RegisterEvidenceTools` → `commute_sa::EvidenceQuery`。  
原始 `get_*_window` 仅保留 C++ API 供调试，**不注册给 Agent**。

### 动作（已注册）

| Tool | 入参 | 出参 |
|------|------|------|
| `get_param_limits` | — | min/max/step |
| `begin_theta_trial` | — | 快照 θ |
| `apply_theta_delta` | `{param, delta, reason}` | old/new；按 step 裁剪并落盘 |
| `evaluate_theta_on_history` | `since_ms?`, `limit?` | 历史反事实 score（越高越好） |
| `revert_theta_trial` / `commit_theta_trial` | — | 回滚或接受试验 |
| `write_audit` | `message`, `changes?` | `audit_id` → `audit.jsonl` |
| `request_anchor_reestimate` | `which: home\|company\|both` | `job_id` → `anchor_reestimate_jobs.jsonl` |

实现：`sa_agent::RegisterActionTools` → `commute_sa::ApplyThetaDeltaAction` / `EvaluateThetaOnHistoryAction` / trial helpers。

推荐闭环：`begin_theta_trial` → eval baseline → `apply_theta_delta` → eval → 变差则 `revert` → 最终 `commit` + `write_audit`。

## 系统提示要点

1. 只改 θ 与触发重估，不直接改业务代码。
2. 每参有 `min/max/step`；单次 Invoke 最多改 K 个参。
3. 必须引用 `get_error_stats` 中的证据。
4. 稳态（连续 N 天达标）建议 `no_op`。

## 自标注（无天天真值）

| 标签 | 定义 |
|------|------|
| `t*_leave_home` | HOME OUTSIDE 持续 `away_confirm_s` 且在外 ≥ `min_away_s` |
| `t*_leave_company` | COMPANY 侧同上 |
| `false_push` | 推送后未确认离开 / 用户关闭 |
| `missed_leave` | 持续 OUTSIDE ≥ `away_confirm_s`，且 lookback 内无对应侧 push |
| `lead` | `t* - t_push` |

`missed_leave` 由 `ProductStore::ObserveMissedLeave` 每 tick 自标注，写入 `leave_episodes.jsonl`，并触发 `MISSED_LEAVE` 改参任务（绕过 6h cooldown）。

## 配置文件

见 `jiuwen_agent/agent_config.json` 与 `jiuwen_agent/tools_contract.json`。

**Agent 能读到哪些语义：** 见 [AGENT_SEMANTICS.md](AGENT_SEMANTICS.md)。
