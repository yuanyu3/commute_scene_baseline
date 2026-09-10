# 架构：HSMM 实时环 + 受约束模板个性化环

## 1. 目标

| 能力 | 要求 |
|------|------|
| 实时场景 | 识别在家 / 离家中 / 在公司 / 离开公司 / 通勤 / 在外 |
| 主动服务 | 仅在 `LEAVING_HOME` / `LEAVING_COMPANY` 窗口推送（如带钥匙） |
| 个性化 | Agent 只组合受支持的时序原语；C++ 选强度并回放验收 |
| 坐标 | 全链路 WGS84（见 `CRS_UNIFICATION.md`） |

## 2. 双环

```text
┌─────────────────────────────────────────────────────────┐
│ SA 感知 tick（本仓库 sa_cpp dump）                        │
│ GPS(WGS84) + PDR + WiFi/BLE/CELL + motion               │
└──────────────────────────┬──────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────┐
│ 基线（无 LLM）                                           │
│ 锚点(home/company) → 关系(INSIDE/NEAR/OUTSIDE)          │
│ → 语义观测 → 显式持续时间 HSMM → 产品门控 → 可选推送     │
└──────────────────────────┬──────────────────────────────┘
                           ▼
              事后自标注 t* + 推送对错日志
                           ▼
┌─────────────────────────────────────────────────────────┐
│ Jiuwen 个性化 Agent（事件/日终触发）                      │
│ 跨 episode 归因 → 组合模板结构（无数值）                  │
└──────────────────────────┬──────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────┐
│ 确定性安全层                                              │
│ DSL 校验 → LOW/MEDIUM/HIGH 全历史 HSMM 回放 → 硬门 → 提交 │
└─────────────────────────────────────────────────────────┘
```

## 3. 状态机

```text
                 LEAVING_HOME
    AT_HOME ──────────────────► COMMUTE / AWAY
       ▲                              │
       │ ARRIVE_HOME                  │
       └──────────────────────────────┘

                 LEAVING_COMPANY
 AT_COMPANY ──────────────────► COMMUTE / AWAY
       ▲                              │
       │ ARRIVE_COMPANY               │
       └──────────────────────────────┘
```

| 状态 | 含义 | 可推送 |
|------|------|--------|
| `AT_HOME` | 在家且非出门过程 | 否 |
| `LEAVING_HOME` | 正在离家过渡 | **是**（带钥匙等） |
| `AT_COMPANY` | 在公司且非离开过程 | 否 |
| `LEAVING_COMPANY` | 正在离开公司 | **是**（可选） |
| `COMMUTE` | 家↔公司走廊上 | 否 |
| `AWAY` | 稳定在外且非上述 | 否 |
| `UNKNOWN` | 证据不足 | 否 |

转移原则：

- 单一行走事实不足以进入 `LEAVING_*`
- 朝锚点距离减小不得判为离开该锚点
- `LEAVING_*` 为短暂过渡；外侧稳定后进入 `COMMUTE`/`AWAY`

## 4. 推送时序（预测离家）

```text
条件：home ∈ {INSIDE,NEAR} 且 P(LEAVING) 达标
      （ETA_out 仅诊断，不再设置过早上限）
      且 非 OUTSIDE（完全离家后不推「带钥匙」）
t_push = 上述条件首次满足的 tick（同一离开 episode 只推一次）
t*     = 推送后首次 OUTSIDE
lead   = t* − t_push
目标：lead ≥ lead_min；只惩罚偏晚，提前更多不再受 lead_max 限制
```

`eta_leave_s`：按外扩速度（或默认步行 1.2m/s）估计距穿过 `r_out` 还有多少秒。  
实时环无 LLM；事后 `lead_s` 写入 label，供改参 Agent 使用。

不是固定闹钟；事件触发为主，时段先验为门控。

## 5. 组件边界（本仓库自洽）

| 组件 | 本仓库位置 | 说明 |
|------|------------|------|
| SA 工程 | `sa_service/` | Ability + dump(WGS84) + PDR/LeaveCar + ProactiveAgent |
| 轻量 dump | `sa_cpp/` | 主机可编，不依赖 OHOS |
| 实时场景 | `sa_cpp/` | HSMM + 产品 FSM，无 LLM |
| 模板 Agent | `jiuwen_agent/` | 低频归因并组合受支持的事件序列 |

提交后的 `active_context_template.json` 在 `SceneEngine` 构造语义观测后、进入 HSMM 前应用。模板不能绕开 OUTSIDE、approaching、attached 等产品门控，也不能修改 HSMM 代码或任意参数。
| 锚点 | `config/anchors.json` | 推断管线 |
| Jiuwen 运行时 | 外部（如 bbpjiuwen-linux） | 仅宿主 |
