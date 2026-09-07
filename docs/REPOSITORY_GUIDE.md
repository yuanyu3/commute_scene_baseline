# 仓库导航与维护约定

这份文档回答三个问题：代码应该改在哪里、哪些内容应该提交、一次改动如何完成验证。

## 1. 源码边界

| 路径 | 角色 | 是否提交 |
|------|------|----------|
| `sa_cpp/` | 共享算法权威源，包含 `commute_sa` 头文件、实现和主机 smoke | 是 |
| `sa_service/` | OHOS System Ability、传感器 provider、主动服务接入 | 是 |
| `sa_service/services/{include,src}/commute_sa/` | 从 `sa_cpp` 同步的 OHOS vendored 副本 | 是，但不要直接修改 |
| `jiuwen_agent/` | 受约束模板 Agent 的 prompt、配置和工具契约 | 是 |
| `examples/` | 可复现的主机流程与 LLM personalizer 示例 | 是 |
| `hap_debug/` | 真机调试 HAP 与 host preview | 是 |
| `config/`、`schemas/` | 默认参数、锚点模板和数据契约 | 是 |
| `docs/` | 产品、架构、采集和算法文档 | 是 |
| `scripts/` | 同步和依赖准备工具 | 是 |

## 2. 本地工作区

以下内容可再生成、体积较大或包含本机信息，由 `.gitignore` 排除：

| 路径/模式 | 内容 |
|-----------|------|
| `data/` | 本地采集数据 |
| `output/` | replay、批处理和 Agent 运行结果 |
| `python/` | 本地离线回放环境 |
| `**/build*/`、`**/out/` | CMake、OHOS 和示例构建产物 |
| `*_smoke_tmp/`、`*_smoke_run/` | smoke 测试落盘 |
| `*.log` | Jiuwen 和服务运行日志 |
| `sa_service/etc/agent.env` | 本机密钥与运行配置 |

需要保留的本地实验结果统一放进 `output/<experiment_name>/`，不要放到仓库根目录或源码目录。

## 3. 共享 C++ 修改流程

1. 只在 `sa_cpp/include/commute_sa/` 和 `sa_cpp/src/` 修改共享算法。
2. 在主机侧构建并运行相应 smoke。
3. 执行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/sync_scene_engine_to_sa.ps1` 更新 OHOS vendored 副本。
4. 执行同一命令并追加 `-Check`，确认没有同步漂移。
5. 根据影响范围运行 `sa_service/services/test/` 中的主机接入测试。

同步脚本只覆盖其显式列出的共享文件；OHOS 专属 provider、Ability、ProactiveAgent 和构建文件不受影响。

## 4. 文档入口

| 主题 | 文档 |
|------|------|
| 项目目标和整体能力 | `PROJECT_INTRO.md` |
| 双环架构 | `ARCHITECTURE.md` |
| 端到端产品流程 | `PRODUCT_FLOW.md` |
| 端侧 tick 与落盘 | `ON_DEVICE_TICK.md` |
| C++ 实时算法 | `CPP_BASELINE.md` |
| HSMM 状态、观测与参数 | `HSMM_BASELINE.md` |
| 前缀就绪、连续提前量评分与冻结回放 | [PREFIX_REPLAY_LEAD_CALIBRATION.md](PREFIX_REPLAY_LEAD_CALIBRATION.md) |
| Agent序列贡献诊断、逐样本保护和组内共享气压 | [TEMPLATE_DIAGNOSTICS_AND_SHARED_BARO.md](TEMPLATE_DIAGNOSTICS_AND_SHARED_BARO.md) |
| Wi-Fi / Cell / BLE 证据 | `RADIO_EVIDENCE.md` |
| PDR 证据 | `PDR_EVIDENCE.md` |
| 当前 Agent 模板个性化 | `CONTEXT_TEMPLATE_PERSONALIZATION.md` |
| 旧直接改参流程（消融对照） | `AGENT_PERSONALIZATION.md`、`AGENT_SEMANTICS.md` |
| 锚点和坐标 | `ANCHOR_INFERENCE.md`、`CRS_UNIFICATION.md` |
| SA 数据采集 | `SA_COLLECTION.md` |

## 5. 提交前检查

```powershell
git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync_scene_engine_to_sa.ps1 -Check
```

确认提交中没有采集数据、运行日志、密钥、构建目录或 smoke 输出；算法改动应同时包含同步后的 `sa_service` vendored 文件。
