# Commute Scene Baseline + Jiuwen Personalization

面向通勤的**端侧 HSMM 实时场景基线**与**低频 θ 个性化**：在用户尚未完全离家时预测出门并推送「带钥匙」等服务；平时不调 LLM，仅在复盘/日终用 Jiuwen 改参。

**本仓库独立，不依赖 `helloworld_agent`。坐标：WGS84。**

> 完整介绍（背景、双环、预测推送、落盘、验证）：**[`docs/PROJECT_INTRO.md`](docs/PROJECT_INTRO.md)**

## 目录

| 路径 | 说明 |
|------|------|
| **`sa_cpp/`** | **算法权威源**：LeaveHsmm / SceneEngine / ProductStore / Evidence·Action / 主机测试 |
| `sa_service/` | OHOS SA（Ability + provider + proactive）；`commute_sa` 为 `sa_cpp` 的 vendored 副本 |
| `jiuwen_agent/` | 改参 Agent（prompt + tools 契约） |
| `examples/personalizer_llm/` | 主机 DeepSeek/Jiuwen 个性化试跑 |
| `hap_debug/` | 真机可视化 HAP（推送 / OUTSIDE / 改参 LLM 时间线） |
| `config/` `schemas/` | θ / 锚点 / Schema |
| `docs/` | 架构与流程文档 |
| `scripts/` | 源码同步与依赖准备脚本 |

本地 `data/`、`output/`、Python 回放环境、构建目录、运行日志与密钥均被忽略，不属于产品源码。完整的提交边界和开发流程见 [`docs/REPOSITORY_GUIDE.md`](docs/REPOSITORY_GUIDE.md)。

## 产品流程（一句话）

```text
采集 → 每 tick HSMM + SceneEngine（无 LLM，预测推送）→ AFTER_PUSH / DAY_END 调 LLM 改 θ
```

细节：[`docs/PRODUCT_FLOW.md`](docs/PRODUCT_FLOW.md)、[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 场景状态

`AT_HOME` / `LEAVING_HOME` / `AT_COMPANY` / `LEAVING_COMPANY` / `COMMUTE` / `AWAY` / `UNKNOWN`

推送仅在 `LEAVING_*` 且仍 **INSIDE/NEAR**（禁止 OUTSIDE 推「带钥匙」）。

## 快速验证

```bash
# SceneEngine smoke（WSL/Linux）
cd sa_cpp && cmake -B build_wsl -DCOMMUTE_SA_BUILD_SMOKE=ON && cmake --build build_wsl
./build_wsl/commute_scene_smoke

# 主机个性化 LLM
bash examples/personalizer_llm/run.sh --debug
```

修改共享算法后同步到 SA：

```powershell
# 检查两份代码是否一致（不写文件）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync_scene_engine_to_sa.ps1 -Check

# 从 sa_cpp 权威源更新 OHOS vendored 副本
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync_scene_engine_to_sa.ps1
```

密钥：复制 `sa_service/etc/agent.env.example` → `agent.env`（勿提交）。
