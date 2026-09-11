# Commute Scene Baseline — 项目介绍

> 当前默认实现采用“Agent 选择干预类型并组织语义结构、确定性工具生成受约束候选并回放验收”。

面向通勤场景的**端侧实时场景基线** + **低频受约束个性化**。在用户**尚未完全离家**时预测出门意图，触发「带钥匙」等主动服务；平时不调大模型，仅在推送复盘 / 日终调用 Jiuwen Agent。

本仓库**独立**，不依赖 `helloworld_agent`。坐标全链路 **WGS84**。

---

## 要解决什么问题

| 问题 | 做法 |
|------|------|
| 何时提醒带钥匙 / 下班？ | 规则化 SceneEngine 在仍 **INSIDE/NEAR** 家或公司时预测推送 |
| 如何避免每秒调 LLM？ | 实时环纯 C++；Agent 只在事件结算或日终低频运行 |
| 如何适应用户习惯？ | 推送后 settle + 证据工具 → Agent 选择结构/强度/时长干预 |
| 误推 / 偏晚怎么收敛？ | 确定性历史回放同时优化正确率与提前量 |

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
│ ② 个性化环（在线 LLM，低频）                               │
│ PersonalizationController → Jiuwen ReAct Agent            │
│ Evidence → 受约束 trial / 回放 → 类型化 audit             │
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

1. 相对位置：家庭和公司统一使用 WGS84 GPS 围栏；定位精度与轨迹跳变决定 GPS 可靠度，`source_type` 仅用于事后标注
2. `LeaveHsmm`：将行走、PDR、距离外扩、**在线 WiFi/Cell 脱离**（见 `docs/RADIO_EVIDENCE.md`）和时段先验作为观测，结合状态持续时间输出 `P(LEAVING)`；距离变近/重新附着强化返回锚点概率
3. `P(LEAVING)` ≥ `enter_leave` → 进入 `LEAVING_*`
4. **预测推送门控**（带钥匙必须在完全离家前）：
   - 仅 `INSIDE` / `NEAR`（**禁止 OUTSIDE 推**）
   - `eta_leave_s` 仅用于诊断，不作为推送上限
   - 同一离开 episode **只推一次**

**ETA**：估计距穿过围栏外径 `r_out` 还有多少秒；只有可靠 GPS 的距离变化才用于速度估计，低可靠度时退回步行/PDR 推断。
**LEAD**：事后 `lead_s = t* − t_push`，其中 `t*` 为推送后首次 `OUTSIDE`；评分在 `[lead_min_s, lead_max_s]` 内奖励更早区分，超过 `lead_max_s` 后按过早扣分，但不用于实时拦截。

---

## 个性化 Agent

配置：`jiuwen_agent/`。主机试跑：`examples/personalizer_llm/`。

| 时机 | 行为 |
|------|------|
| 每个 tick | 不调 LLM |
| `should_service` | 本地推送，不调 LLM |
| 推送后首次 `OUTSIDE` | 标 `CONFIRMED_LEAVE`，**立刻**调 Agent |
| 推送后约 20 min 仍未离开 | 标 `FALSE_PUSH`，调 Agent |
| `DAY_END`（≥22 点） | 调 Agent |

工具分三类：

- **Evidence**：`get_theta` / `get_error_stats` / `get_leave_episode` / sensor windows …
- **受约束干预**：上下文模板、evidence strength 和 HSMM duration 的 trial / commit / discard
- **审计**：`submit_agent_analysis` 明确记录 `STRUCTURE` / `EVIDENCE_STRENGTH` / `DURATION` / `NO_OP`

Agent 负责跨 episode 解释证据、选择一个干预族并说明理由；工具只允许白名单候选，并在历史前缀上评估正确率、误推与提前量。只有验收通过的候选才会提交。

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
| `param_changes.jsonl` / `audit.jsonl` | 受约束候选变更与类型化审计 |
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
3. **Agent 不直接写数值**：只选择干预族、组合结构和解释证据；候选值与验收由确定性工具控制。
4. **坐标统一 WGS84**：见 [`CRS_UNIFICATION.md`](CRS_UNIFICATION.md)。  
5. **与 helloworld 解耦**：产品流是 SceneEngine 主导，不是每 tick Agent。

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | 双环、状态机、预测推送时序 |
| [`PRODUCT_FLOW.md`](PRODUCT_FLOW.md) | 采集 → 判定 → 改参、落盘 |
| [`CPP_BASELINE.md`](CPP_BASELINE.md) | C++ 模块与 SA 接线 |
| [`AGENT_PERSONALIZATION.md`](AGENT_PERSONALIZATION.md) | 当前受约束个性化 Agent |
| [`ON_DEVICE_TICK.md`](ON_DEVICE_TICK.md) | 端上 tick 路径 |
| [`SA_COLLECTION.md`](SA_COLLECTION.md) | 采集与 DEBUG sinks |
| [`examples/personalizer_llm/README.md`](../examples/personalizer_llm/README.md) | 主机 LLM DEBUG / 导出 |
