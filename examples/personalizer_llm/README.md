# Host θ personalizer (real LLM) + DEBUG

Linux/WSL only — links `third_party/bbpjiuwen/lib/libbbpjiuwen.so`.

## 普通跑

```bash
cd /mnt/d/huawei/commute_scene_baseline
bash examples/personalizer_llm/run.sh
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
| `run_data/param_changes.jsonl` | 实际改参 |
| `run_data/audit.jsonl` | 审计 |
| `run_data/theta.json` | 改后的 θ |

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
- `param_changes.jsonl` / `audit.jsonl` / `theta.json`

需要更细 CSV 时：`SA_AGENT_DEBUG_SINKS=1`（与 LLM DEBUG 无关，是传感器 verbose 落盘）。
