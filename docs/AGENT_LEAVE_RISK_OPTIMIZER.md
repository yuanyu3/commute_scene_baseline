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

Outputs include the aggregate dataset profile, API response trace, candidate
evaluations, Agent review, and the selected portable model. The API key is read at
runtime and is never written to output. Use a separate output directory
for each run so the experiment remains auditable.
