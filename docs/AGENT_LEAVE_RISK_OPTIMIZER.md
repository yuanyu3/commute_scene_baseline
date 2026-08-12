# Remote Agent Leave-Risk Optimizer

`examples/agent_optimize_leave_risk.py` connects the offline evaluator to the
OpenAI-compatible endpoint configured in `sa_service/etc/agent.env`.

The remote Agent does not receive raw sensor streams and cannot write production
configuration. It proposes a bounded set of feature-group/threshold candidates,
then reviews locally measured grouped cross-validation results. The local process
rejects invalid candidates and refuses any Agent selection with false-push groups
or missed leave groups.

`SA_AGENT_API_KEY` may be either a bare key or an existing `Bearer <key>` value;
the client normalizes both forms to exactly one `Bearer` prefix.

Run:

```powershell
python examples\agent_optimize_leave_risk.py `
  --env-file sa_service\etc\agent.env `
  --out output\agent_leave_risk_v1
```

For a combined multi-day dataset, pass a manifest:

```powershell
python examples\agent_optimize_leave_risk.py `
  --dataset-manifest config\leave_risk_dataset_0811_0812.json `
  --env-file sa_service\etc\agent.env `
  --out output\agent_leave_risk_0811_0812
```

`GPS source_type=2` is the company-inside state and `source_type=1` is the
company-outside state. The only departure truth is the first `2 -> 1`
transition. A session beginning at `type=1` is a return-to-company trace and is
excluded from departure prediction.

Outputs include the aggregate dataset profile, API response trace, candidate
evaluations, Agent review, and the selected portable model. The API key is read at
runtime and is never written to output. Use a separate output directory
for each run so the experiment remains auditable.
