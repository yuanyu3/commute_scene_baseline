# Host constrained-template personalizer (real LLM)

Linux/WSL only — links `third_party/bbpjiuwen/lib/libbbpjiuwen.so`.

## 普通跑

```bash
cd /mnt/d/commute_scene_baseline
bash examples/personalizer_llm/run.sh /mnt/d/agent.env \
  /mnt/d/commute_scene_baseline/output/context_template_0812_history --no-fixture
```

## DEBUG：看中间过程

```bash
# 方式 A
PERSONALIZER_DEBUG=1 bash examples/personalizer_llm/run.sh

# 方式 B
bash examples/personalizer_llm/run.sh --debug

# 方式 C（显式路径）
PERSONALIZER_DEBUG=1 bash examples/personalizer_llm/run.sh \
  sa_service/etc/agent.env examples/personalizer_llm/run_data --debug
```

DEBUG 打开后你会看到：

| 输出 | 内容 |
|------|------|
| 终端 `===== TURN =====` | 每一轮 ReAct |
| `>>> TOOL START` / `<<< TOOL END` | 工具名、args、result |
| `----- ASSISTANT -----` | 模型中间推理文本 |
| jiuwen `LOG(DEBUG)` | 框架内部日志（stderr） |
| `run_data/agent_trace.jsonl` | **每次运行都会写** 的完整 stream（每行一条）；`--debug` 额外开 TRACE + 更详细终端输出 |
| `run_data/active_context_template.json` | 当前通过回放验收的模板 |
| `run_data/context_templates.jsonl` | 模板提交历史与前后指标 |
| `run_data/param_changes.jsonl` | 普通链路应为空；只供旧改参消融实验 |
| `run_data/audit.jsonl` | 审计 |
| `run_data/theta.json` | 基线 θ；模板链路不修改它 |

把 trace 拉到 Windows 看：

```powershell
code D:\huawei\commute_scene_baseline\examples\personalizer_llm\run_data\agent_trace.jsonl
```

## Stream 事件类型（payload.type）

- `agent` + `turn_start` / `turn_end`：一轮开始/结束  
- `assistant`：模型输出（含 tool 规划说明）  
- `tool` + `status=start|end`：工具调用与返回  

## 手机端 SA

Personalize Invoke 后看 Hilog；落盘看：

- `/data/service/el1/public/commuteagentservice/personalize_jobs.jsonl`
- `active_context_template.json` / `context_templates.jsonl` / `audit.jsonl`

需要更细 CSV 时：`SA_AGENT_DEBUG_SINKS=1`（与 LLM DEBUG 无关，是传感器 verbose 落盘）。
