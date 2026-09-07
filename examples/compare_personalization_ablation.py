#!/usr/bin/env python3
"""Compare fixed theta, deterministic ES-CRO, and an Agent semantic plan.

The script deliberately uses the same C++ HSMM replay backend for every arm.
It does not call an LLM. Export the Agent's semantic plan JSON from a Jiuwen run
and pass it with --agent-plan, so evidence/search budgets can be controlled by
the experiment runner.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def call_tool(binary: Path, root: Path, command: str, params: dict[str, Any]) -> dict[str, Any]:
    proc = subprocess.run(
        [str(binary), str(root), command, json.dumps(params, ensure_ascii=False)],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"{command} returned no output")
    return json.loads(lines[-1])


def best_metrics(result: dict[str, Any]) -> dict[str, Any] | None:
    best_id = result.get("best_candidate_id")
    for candidate in result.get("candidates", []):
        if candidate.get("id") == best_id:
            return candidate.get("metrics")
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path, help="commute_offline_tools executable")
    parser.add_argument("--root", required=True, type=Path, help="dataset/product root")
    parser.add_argument("--agent-plan", type=Path, help="Agent semantic plan JSON; no numeric theta values")
    parser.add_argument("--output", type=Path, help="optional JSON report")
    parser.add_argument("--limit", type=int, default=30)
    args = parser.parse_args()

    baseline = call_tool(args.binary, args.root, "evaluate", {"limit": args.limit})
    rule_analysis = call_tool(args.binary, args.root, "rules", {"limit": args.limit})
    rule_result = call_tool(args.binary, args.root, "rule_optimize", {"limit": args.limit})
    report: dict[str, Any] = {
        "protocol": "same C++ HSMM replay and commit metrics; no parameter is committed",
        "fixed_baseline": baseline,
        "rule_analysis": rule_analysis,
        "rule_optimizer": rule_result,
        "rule_best_metrics": best_metrics(rule_result),
    }

    if args.agent_plan:
        plan = json.loads(args.agent_plan.read_text(encoding="utf-8"))
        forbidden = {key for key in plan if key.startswith("w_") or key in {"enter_leave", "exit_leave"}}
        if forbidden:
            raise ValueError(f"Agent plan contains forbidden numeric theta fields: {sorted(forbidden)}")
        plan["max_candidates"] = min(int(plan.get("max_candidates", 12)), 20)
        agent_result = call_tool(args.binary, args.root, "agent_optimize", plan)
        report["agent_plan"] = plan
        report["agent_optimizer"] = agent_result
        report["agent_best_metrics"] = best_metrics(agent_result)

    body = json.dumps(report, ensure_ascii=False, indent=2)
    print(body)
    if args.output:
        args.output.write_text(body + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
