# Commute Scene Baseline + Constrained Agent Personalization

面向通勤的**端侧 HSMM 实时场景基线**与**低频受约束个性化**：在用户尚未完全离开锚点时预测离开并推送服务；实时环不调用 LLM，复盘时由 Agent 总结个人传感器时序，C++ 负责验证、选择强度并落盘模板。

**算法权威源在本仓库 `sa_cpp/`。真机 SA 宿主是 `D:\helloworld_agent`（SA 9902）**，不要再刷本仓库 `sa_service/` 的 9903。

> 当前实现入口：**[`docs/README.md`](docs/README.md)**；受约束个性化链路：**[`docs/CONTEXT_TEMPLATE_PERSONALIZATION.md`](docs/CONTEXT_TEMPLATE_PERSONALIZATION.md)**。

## 目录

| 路径 | 说明 |
|------|------|
| **`sa_cpp/`** | **算法权威源**：LeaveHsmm / SceneEngine / ProductStore / Evidence·Action / 主机测试 |
| `sa_service/` | OHOS SA 参考实现（9903 命名）；真机请用 `helloworld_agent` 9902 |
| `jiuwen_agent/` | 上下文模板 Agent（prompt + tools 契约） |
| `examples/personalizer_llm/` | 主机 DeepSeek/Jiuwen 个性化试跑 |
| `hap_debug/` | 可视化 HAP 参考；真机合并版在 `helloworld_agent/hap` |
| `config/` `schemas/` | θ / 锚点 / Schema |
| `docs/` | 架构与流程文档 |
| `scripts/` | 源码同步与依赖准备脚本 |

本地 `data/`、`output/`、Python 回放环境、构建目录、运行日志与密钥均被忽略，不属于产品源码。完整的提交边界和开发流程见 [`docs/REPOSITORY_GUIDE.md`](docs/REPOSITORY_GUIDE.md)。

## 产品流程（一句话）

```text
采集 → 每 tick HSMM + SceneEngine（无 LLM）→ 事后 Agent 合成模板 → C++ 全历史回放 → 安全提交
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

# 主机模板个性化 LLM（WSL；参数依次为 env 与产品数据根目录）
bash examples/personalizer_llm/run.sh /mnt/d/agent.env \
  /mnt/d/commute_scene_baseline/output/context_template_0812_history --no-fixture --debug
```

修改共享算法后同步到 SA：

```powershell
# 检查两份代码是否一致（不写文件）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync_scene_engine_to_sa.ps1 -Check

# 从 sa_cpp 权威源更新 OHOS vendored 副本
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync_scene_engine_to_sa.ps1
```

密钥：复制 `sa_service/etc/agent.env.example` → `agent.env`（勿提交）。文档总入口见 [`docs/README.md`](docs/README.md)，新个性化链路见 [`docs/CONTEXT_TEMPLATE_PERSONALIZATION.md`](docs/CONTEXT_TEMPLATE_PERSONALIZATION.md)。旧的直接改 θ 工具仍留作消融对照，但普通 Agent 宿主已禁止调用。
