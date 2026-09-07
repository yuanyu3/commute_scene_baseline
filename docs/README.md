# 文档导航

## 当前实现（优先阅读）

| 主题 | 文档 |
|---|---|
| 总体架构与组件边界 | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Agent 受约束模板个性化 | [CONTEXT_TEMPLATE_PERSONALIZATION.md](CONTEXT_TEMPLATE_PERSONALIZATION.md) |
| Agent 模板同步版本与验证边界 | [VERSION_AGENT_TEMPLATE_SYNC.md](VERSION_AGENT_TEMPLATE_SYNC.md) |
| HSMM 状态、观测与时长 | [HSMM_BASELINE.md](HSMM_BASELINE.md) |
| 阶段 1/2 实现与前后消融 | [STAGE1_STAGE2_ABLATION.md](STAGE1_STAGE2_ABLATION.md) |
| C++ 实时基线 | [CPP_BASELINE.md](CPP_BASELINE.md) |
| 产品触发与数据落盘 | [PRODUCT_FLOW.md](PRODUCT_FLOW.md)、[ON_DEVICE_TICK.md](ON_DEVICE_TICK.md) |
| 仓库边界与同步规则 | [REPOSITORY_GUIDE.md](REPOSITORY_GUIDE.md) |

## 传感器与基础设施

- [RADIO_EVIDENCE.md](RADIO_EVIDENCE.md)：Wi-Fi、Cell、BLE。
- [PDR_EVIDENCE.md](PDR_EVIDENCE.md)：步行与外向位移。
- [ANCHOR_INFERENCE.md](ANCHOR_INFERENCE.md)：家庭/公司锚点。
- [CRS_UNIFICATION.md](CRS_UNIFICATION.md)：坐标系。
- [SA_COLLECTION.md](SA_COLLECTION.md)：OHOS 数据采集。

## 对照与研究材料

`AGENT_PERSONALIZATION.md`、`AGENT_SEMANTICS.md`、`AGENT_LEAVE_RISK_OPTIMIZER.md` 记录旧的“Agent/规则直接改 θ”方案，代码保留用于消融实验，不代表普通生产 Agent 的权限。中文专利与实验文档属于阶段性研究材料；实现事实以源码和“当前实现”文档为准。
