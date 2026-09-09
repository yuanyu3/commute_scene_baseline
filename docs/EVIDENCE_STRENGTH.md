# Evidence Strength

## 当前语义

HSMM 各原子观测通道使用独立的 evidence_strength，取值范围为 [0,1]：

- 0：该观测不进入发射似然；
- 0.2：很弱影响；
- 0.5：中等影响；
- 1：完整使用全局 HSMM emission 信息。

各通道不需要归一化，总和也不要求等于1。HSMM直接计算
strength_i × log P(x_i | state)，运行时不再应用隐藏的 0.25 + 3*w 映射。

当前 C++ Theta 内仍保留 w_* 成员名以兼容已有源码，但这些成员从本版本开始承载的已经是
直接 evidence strength。新配置与持久化文件使用：

~~~json
{
  "evidence_strength": {
    "walking": 1.0,
    "pdr": 0.85,
    "geo": 0.85,
    "wifi": 0.61,
    "cell": 0.49,
    "ble": 0.0,
    "time": 0.85,
    "baro": 0.85
  }
}
~~~

## 旧配置迁移

只有缺少 evidence_strength 对象的旧 theta.json 才按以下公式读取：

~~~text
legacy w <= 0 → strength = 0
legacy w > 0  → strength = clamp(0.25 + 3*w, 0, 1)
~~~

迁移后的默认值保持旧基线有效可靠度：walking=1、pdr/geo/time/baro=0.85、
wifi=0.61、cell=0.49、ble=0。下一次持久化会写成新格式，不会再次转换。

新格式中的弱值保持线性，例如 wifi=0.01 就以0.01进入HSMM；wifi=0
同时在 SceneEngine 观测构造、模板后通道掩码和 HSMM 三层禁用该通道。

## 锚点级个性化

Agent只能选择需要重新拟合的通道族并调用 `estimate_evidence_strength`。确定性估计器按
episode统计 pos_hit/pos_valid/neg_hit/neg_valid，执行Laplace平滑、coverage与8条样本的
shrinkage，并把单次变化限制在全局值±0.30。缺字段不进入valid；baro还必须明确available，
geo必须relation_known。已经通过物理证据解释为 ABORTED_LEAVE 的返回过程从强度正负统计中
排除，避免把“可信离开前缀后折返”错误学习为某个原子证据不可靠。

候选使用同一份历史完整重放HSMM。除聚合误推、漏报不得增加且确认离开不得减少外，逐episode
还禁止新增误推、丢失正例或推迟已有正例推送。满足安全门后才允许
`commit_evidence_strength_candidate`。提交结果按真实ID写入
`user_anchor_profile_<anchor_id>.json`；运行时只覆盖匹配 anchor_id 的强度，因此不同锚点互不影响。
新历史行固化 `anchor_id`；旧的 company/home 行通过当前 `anchors.json` 映射到对应真实ID，原日志不被改写。
Profile读取有进程内缓存，不在逐tick读取文件。

同一个 `UserAnchorProfile` 现在也可保存 `duration_prior.pre_leave_mean_s` 与
`duration_prior.leaving_mean_s`。Evidence Strength 与 Duration 每次只能选择一个主要干预族，
各自生成候选并独立回放，提交时保留另一类已有覆盖值。
