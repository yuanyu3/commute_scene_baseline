# HSMM Duration 个性化

运行时先读取全局 `Theta`，再只按 `anchor_id` 叠加
`UserAnchorProfile.duration_prior`。第一版只开放：

- `PRE_LEAVE` 的 mean duration；
- `LEAVING` 的 mean duration。

Agent 只能调用 `fit_duration_prior(anchor_id, state)` 选择需要重估的状态，不能提交数值、
方向或上下界。C++ 使用当前基线重放已确认离开/漏报正例，从四状态后验路径提取未左截断的
PRE_LEAVE 段，以及从 LEAVING 主导开始到 OUTSIDE/真实离开时刻的时长。

估计器使用 median、10% trimmed mean 与 MAD 抑制离群值。少于3条有效样本保持全局值且禁止提交；
3–4条、5–7条、8条以上分别使用强、中、较弱收缩。最终均值相对当前基线最多改变50%，并受
HSMM原有 min/max 边界约束。

候选不会直接影响实时配置。工具会完整重放同一历史，检查聚合误推/漏报/确认离开，并逐episode
禁止新增误推、丢失正例、推迟已有正例推送。只有 `eligible=true` 才能调用
`commit_duration_prior_candidate`；否则丢弃或 no-op。

Profile 示例：

~~~json
{
  "schema_version": 1,
  "anchor_id": "company_001",
  "evidence_strength": {"wifi": 0.31, "baro": 0.85},
  "duration_prior": {"leaving_mean_s": 156},
  "feature_thresholds": {}
}
~~~
