# Jiuwen 改参 Agent

## 定位

- **不做**每 tick 场景推理（由基线完成）。
- **只在**误差门控 / 日终 / 锚点重估时 `Invoke`。
- 使用 Jiuwen `AgentType::REACT` 或 `PLAN_EXECUTE`，`maxTurn≥3`，**启用 Tool**。

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
| `get_wifi_window` | `t_center_ms?`, `before_s`, `after_s`, `session_dir?`, `limit?` | raw CSV 行 |
| `get_cell_window` | 同上 | raw CELL |
| `get_mag_window` | 同上 | raw 磁 |
| `get_gps_window` | 同上 | raw location |

实现：`sa_agent::RegisterEvidenceTools` → `commute_sa::EvidenceQuery`（读 product 根目录 + Ability session dump）。

### 动作（已注册）

| Tool | 入参 | 出参 |
|------|------|------|
| `get_param_limits` | — | min/max/step |
| `apply_theta_delta` | `{param, delta, reason}` | old/new；按 step 裁剪并落盘 |
| `write_audit` | `message`, `changes?` | `audit_id` → `audit.jsonl` |
| `request_anchor_reestimate` | `which: home\|company\|both` | `job_id` → `anchor_reestimate_jobs.jsonl` |

实现：`sa_agent::RegisterActionTools` → `commute_sa::ApplyThetaDeltaAction` / `WriteAuditAction` / `RequestAnchorReestimateAction`。

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
| `missed_leave` | 确认离开但未推送 |
| `lead` | `t* - t_push` |

## 配置文件

见 `jiuwen_agent/agent_config.json` 与 `jiuwen_agent/tools_contract.json`。
