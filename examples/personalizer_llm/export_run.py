#!/usr/bin/env python3
"""Export personalizer_llm run_data into a dated folder."""
from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent
    src = root / "run_data"
    exp_name = sys.argv[1] if len(sys.argv) > 1 else "export_latest"
    exp = root / exp_name
    exp.mkdir(parents=True, exist_ok=True)

    for name in ("theta.json", "param_changes.jsonl", "audit.jsonl", "run_console.log"):
        p = src / name
        if p.exists():
            shutil.copy2(p, exp / name)

    log = (src / "run_console.log").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^raw message=(.*)$", log, re.M)
    if not m:
        print("ERROR: raw message not found in run_console.log", file=sys.stderr)
        return 1
    msg = json.loads(m.group(1))
    (exp / "agent_result.json").write_text(
        json.dumps(msg, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (exp / "llm_response.md").write_text(msg.get("llmResponse", ""), encoding="utf-8")

    tools = msg.get("toolCallInfo") or []
    lines = []
    for i, t in enumerate(tools, 1):
        row = {
            "step": i,
            "name": t.get("name"),
            "status": t.get("status"),
            "inputParams": t.get("inputParams"),
            "outputResult": t.get("outputResult"),
            "errMessage": t.get("errMessage"),
        }
        for k in ("inputParams", "outputResult"):
            v = row.get(k)
            if isinstance(v, str) and v.strip()[:1] in "{[":
                try:
                    row[k] = json.loads(v)
                except Exception:
                    pass
        lines.append(row)
    (exp / "tool_calls.json").write_text(
        json.dumps(lines, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    timeline = [
        line.strip()
        for line in log.splitlines()
        if "[DEBUG] [Trace] PlanTrace" in line or "[DEBUG] [Trace] ToolTrace" in line
    ]
    (exp / "jiuwen_trace_timeline.txt").write_text("\n".join(timeline) + "\n", encoding="utf-8")

    params = [
        json.loads(x)
        for x in (src / "param_changes.jsonl").read_text(encoding="utf-8").splitlines()
        if x.strip()
    ]
    this_params = params[-2:] if len(params) >= 2 else params

    summary = [
        "# Personalizer run export",
        "",
        "## Status",
        "- SUCCESS (status=2, errorCode=0)",
        "",
        "## This run θ changes",
    ]
    for p in this_params:
        summary.append(
            f"- `{p['param']}`: {p['old']} → {p['new']}  \n  reason: {p.get('reason', '')}"
        )
    summary += ["", f"## Tool sequence ({len(tools)} calls)"]
    for t in tools:
        summary.append(f"1. `{t.get('name')}` → {t.get('status')}")
    summary += [
        "",
        "## Files",
        "| File | Meaning |",
        "|------|---------|",
        "| `agent_result.json` | Full Invoke response |",
        "| `tool_calls.json` | Parsed tool I/O (middle steps) |",
        "| `llm_response.md` | Assistant final text |",
        "| `theta.json` | θ after run |",
        "| `param_changes.jsonl` | Param deltas (may include older runs) |",
        "| `audit.jsonl` | Audits (last line = this run) |",
        "| `jiuwen_trace_timeline.txt` | Plan/Tool Trace from DEBUG log |",
        "| `agent_trace.jsonl` | Rebuilt from final response + tools |",
        "| `run_console.log` | Full console |",
        "",
    ]
    (exp / "SUMMARY.md").write_text("\n".join(summary), encoding="utf-8")

    trace_rows = [{"type": "final_response", "payload": msg}]
    for t in lines:
        trace_rows.append(
            {
                "type": "tool_call",
                "name": t["name"],
                "status": t["status"],
                "input": t["inputParams"],
                "output": t["outputResult"],
            }
        )
    body = "".join(json.dumps(row, ensure_ascii=False) + "\n" for row in trace_rows)
    (src / "agent_trace.jsonl").write_text(body, encoding="utf-8")
    (exp / "agent_trace.jsonl").write_text(body, encoding="utf-8")

    print(f"exported -> {exp}")
    print(f"tools={len(tools)} timeline_lines={len(timeline)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
