# Commute Debug HAP

可视化通勤 SA 的 **SceneEngine 场景变化 / 推送 / OUTSIDE 确认 / 改参 LLM** 时间线。

## 依赖

1. 设备已刷入并启动 `commuteagentservice`（SA 9903）
2. NAPI 已带上 `GetProductDebugTimeline` / `ClearProductDebugTimeline`
3. DevEco Studio（API 12+）打开本目录编译签名安装

## 界面

- 顶部 **Status**：当前 scene、家/公司关系、分数、ETA、最近 LLM 状态
- 下方 **Timeline**：`SCENE` / `PUSH` / `OUTSIDE` / `LABEL` / `LLM_START` / `LLM_DONE`
- 1 秒轮询 `commuteagentservice.GetProductDebugTimeline()`

## 编译安装

```text
DevEco → Open → hap_debug/
签名（建议 system 或与 dump HAP 同权限）→ Run
```

`module.json5` 中 `commuteagentservice` 为系统 kit，需设备侧 ACL / 系统应用权限；若 NAPI 不可见，把
`sa_service/interfaces/declaration/api/@ohos.commuteagentservice.d.ts` 拷到 SDK api 目录，或本地 `oh_modules` 引用。

## 无真机时

打开 [`host_preview/index.html`](host_preview/index.html)，用模拟事件感受 UI；或粘贴 SA 返回的 JSON。

## 点开 PersonalizeInvoke

`LLM_DONE`（title 常为 `PersonalizeInvoke`）事件带 `result`：

- `changes[]`：`param` / `old` / `new` / `reason`（来自 `apply_theta_delta`）
- `audits[]`：`write_audit` 消息（含 no_op）
- `response_summary`：模型最终回复摘要

HAP 中点击该条即可展开。
