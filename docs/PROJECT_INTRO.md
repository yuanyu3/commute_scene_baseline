# Commute Scene Baseline — 项目介绍

面向通勤场景的**端侧实时场景基线** + **低频 θ 个性化**。在用户**尚未完全离家**时预测出门意图，触发「带钥匙」等主动服务；平时不调大模型，仅在推送复盘 / 日终用 Jiuwen Agent 微调参数。

本仓库**独立**，不依赖 `helloworld_agent`。坐标全链路 **WGS84**。

---

## 要解决什么问题

| 问题 | 做法 |
|------|------|
| 何时提醒带钥匙 / 下班？ | 规则化 SceneEngine 在仍 **INSIDE/NEAR** 家或公司时预测推送 |
| 如何避免每秒调 LLM？ | 实时环纯 C++；LLM 只改 θ，且由流程门控 |
| 如何适应用户习惯？ | 推送后 settle + 证据工具 → Agent 小步改 `theta.json` |
| 误推 / 偏晚怎么收敛？ | `FALSE_PUSH` / `lead_s` 统计驱动改参 |

**不做：** 用大模型做每 tick 场景分类；不把「回家过程」做成独立推送态（到达落成 `AT_HOME` / `AT_COMPANY`）。

---

## 双环架构

```text
┌──────────────────────────────────────────────────────────┐
│ ① 实时环（无 LLM）                                        │
│ SA 感知 tick → 特征 → SceneEngine → 场景 / 可选推送        │
└───────────────────────────┬──────────────────────────────┘
                            │ 首次 OUTSIDE / 误推超时 / 日终
                            ▼
┌──────────────────────────────────────────────────────────┐
│ ② 改参环（在线 LLM，低频）                                 │
│ PersonalizationController → Jiuwen ReAct Agent            │
│ Evidence / Action Tools → 更新 θ / audit                  │
└──────────────────────────────────────────────────────────┘
```

详见 [`ARCHITECTURE.md`](ARCHITECTURE.md)、[`PRODUCT_FLOW.md`](PRODUCT_FLOW.md)。

---

## 实时判定（SceneEngine）

默认 `focus_side=company`：只启用公司侧离开候选与推送；家侧 relation 仍可计算但不推 `DEPARTURE_NOTIFICATION`。

权威实现：`sa_cpp/`（同步至 `sa_service/.../commute_sa/`）。

### 场景状态

| 状态 | 含义 | 可推送 |
|------|------|--------|
| `AT_HOME` | 在家且非出门过程 | 否 |
| `LEAVING_HOME` | 正在离家（预测过渡） | **是**（带钥匙等） |
| `AT_COMPANY` / `LEAVING_COMPANY` | 公司侧对称 | 离开公司可推 |
| `COMMUTE` / `AWAY` / `UNKNOWN` | 通勤 / 在外 / 不足 | 否 |

### 单 tick 流程（简述）

1. 相对位置：家侧仍用 GPS 围栏；**公司侧在附近时以 `source_type` 为准**（`2`=公司内，`1`=公司外 / 出大门），`r_in`/`r_out` 只作附近辅助
2. `LeaveHsmm`：将行走、PDR、距离外扩、**在线 WiFi/Cell 脱离**（见 `docs/RADIO_EVIDENCE.md`）和时段先验作为观测，结合状态持续时间输出 `P(LEAVING)`；距离变近/重新附着强化返回锚点概率
3. `P(LEAVING)` ≥ `enter_leave` 且过 `arm_delay` → 进入 `LEAVING_*`
4. **预测推送门控**（带钥匙必须在完全离家前）：
   - 仅 `INSIDE` / `NEAR`（**禁止 OUTSIDE 推**）
   - `eta_leave_s` ≤ `lead_max_s`（太早不推）
   - 同一离开 episode **只推一次**

**ETA**：家侧估计距穿过围栏外径 `r_out` 还有多少秒；公司侧室内（`source_type=2`）不采信围栏 ETA，GNSS（`1`）视为已出大门。  
**LEAD**：事后 `lead_s = t* − t_push`，其中 `t*` 为推送后首次 `OUTSIDE`；目标约 `lead_min_s`～`lead_max_s`（默认 90～240s）。

---

## 个性化 Agent

配置：`jiuwen_agent/`。主机试跑：`examples/personalizer_llm/`。

| 时机 | 行为 |
|------|------|
| 每个 tick | 不调 LLM |
| `should_service` | 本地推送，不调 LLM |
| 推送后首次 `OUTSIDE` | 标 `CONFIRMED_LEAVE`，**立刻**调 θ LLM |
| 推送后约 20 min 仍未离开 | 标 `FALSE_PUSH`，调 θ LLM |
| `DAY_END`（≥22 点） | 调 θ LLM |

工具分两类：

- **Evidence**：`get_theta` / `get_error_stats` / `get_leave_episode` / sensor windows …
- **Action**：`apply_theta_delta` / `write_audit` / `request_anchor_reestimate` …

策略示例：误推提高 `enter_leave` 或降低噪声通道 `w_*`；确认离开但 lead 偏小则略降阈值或 `arm_delay`。改参后应用 `evaluate_theta_on_history` 在历史离开窗口上回放 HSMM，变差则 `revert_theta_trial`。

---

## 仓库结构

| 路径 | 说明 |
|------|------|
| `sa_service/` | OHOS SA 工程（Ability、采集、ProactiveAgent + SceneEngine） |
| `sa_cpp/` | 共享 C++：SceneEngine、ProductStore、Evidence/Action、主机 smoke |
| `jiuwen_agent/` | 改参 system prompt + tools 契约 |
| `examples/personalizer_llm/` | 主机 DeepSeek/Jiuwen 个性化试跑与导出 |
| `hap_debug/` | 调试 HAP：轮询 Scene / Push / Label / LLM 时间线 |
| `python/commute_baseline/` | **可选**离线回放（运行时不依赖） |
| `config/` `schemas/` | 默认 θ、锚点、Schema |
| `docs/` | 架构、流程、CRS、采集、改参等 |

`sa_cpp` → `sa_service` 同步：`scripts/sync_scene_engine_to_sa.ps1`。

---

## 产品落盘（最小集）

设备根目录示例：`/data/service/el1/public/commuteagentservice/`

| 文件 | 用途 |
|------|------|
| `anchors.json` | 家/公司锚点（WGS84） |
| `theta.json` | SceneEngine 参数 |
| `leave_episodes.jsonl` | push + label（含 `eta_leave_s` / `t_star_ms` / `lead_s`） |
| `leave_window_samples.jsonl` | 推送后稀疏 GPS/行走 |
| `param_changes.jsonl` / `audit.jsonl` | 改参与审计 |
| `personalize_jobs.jsonl` | 改参任务队列 |

默认不写每 tick 的 verbose dump；排查时设 `SA_AGENT_DEBUG_SINKS=1`。凭证见 `sa_service/etc/agent.env`（勿提交密钥）。

---

## 如何快速验证

**C++ SceneEngine smoke（预测推送）：**

```bash
cd sa_cpp
cmake -B build_wsl -DCOMMUTE_SA_BUILD_SMOKE=ON   # Linux/WSL
cmake --build build_wsl
./build_wsl/commute_scene_smoke
```

**主机跑个性化 LLM：**

```bash
# WSL / Linux，需已 sync libbbpjiuwen 与 agent.env
bash examples/personalizer_llm/run.sh --debug
# 结果见 examples/personalizer_llm/run_data/ 与 export_* /
```

**可选 Python 回放：** 见根目录 [`README.md`](../README.md)。

---

## 设计原则（摘要）

1. **实时可解释、可测**：规则 + θ，不用黑盒场景 LLM。  
2. **服务时机**：预测离家，人还在家时提醒。  
3. **LLM 只调参**：证据工具约束，单次最多少量 `apply_theta_delta`。  
4. **坐标统一 WGS84**：见 [`CRS_UNIFICATION.md`](CRS_UNIFICATION.md)。  
5. **与 helloworld 解耦**：产品流是 SceneEngine 主导，不是每 tick Agent。

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | 双环、状态机、预测推送时序 |
| [`PRODUCT_FLOW.md`](PRODUCT_FLOW.md) | 采集 → 判定 → 改参、落盘 |
| [`CPP_BASELINE.md`](CPP_BASELINE.md) | C++ 模块与 SA 接线 |
| [`AGENT_PERSONALIZATION.md`](AGENT_PERSONALIZATION.md) | 改参 Agent |
| [`ON_DEVICE_TICK.md`](ON_DEVICE_TICK.md) | 端上 tick 路径 |
| [`SA_COLLECTION.md`](SA_COLLECTION.md) | 采集与 DEBUG sinks |
| [`examples/personalizer_llm/README.md`](../examples/personalizer_llm/README.md) | 主机 LLM DEBUG / 导出 |
