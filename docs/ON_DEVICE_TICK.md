# 端上跑：采集 → SceneEngine →（流程触发）改参 LLM

详见 [`PRODUCT_FLOW.md`](PRODUCT_FLOW.md)。

```text
每 tick:
  SceneEngine → 可选 Push
  （不调 LLM；默认不写每 tick CSV）

推送后离开窗口:
  leave_episodes.jsonl (push) + leave_window_samples.jsonl

流程触发（首次 OUTSIDE→CONFIRMED_LEAVE 立刻改参；20min 无 OUTSIDE→FALSE_PUSH；或 DAY_END）:
  leave_episodes.jsonl (label) + personalize_jobs.jsonl + Invoke(θ 更新)
```

`agent.env` 仅凭证与可选 `SA_AGENT_DEBUG_SINKS`，**不是** LLM 调用开关。
