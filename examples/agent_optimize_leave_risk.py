#!/usr/bin/env python3
"""Use a remote OpenAI-compatible Agent to propose and review leave-risk policies.

The Agent only sees aggregate dataset/evaluation summaries. Sensor parsing, grouped
cross-validation, safety checks, and model export remain local and deterministic.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.leave_risk import feature_names, load_event_rows  # noqa: E402
from train_leave_risk import (  # noqa: E402
    candidate_score,
    cross_validate,
    fit_models,
    route_holdout,
    serialize_model,
    summarize,
)


FEATURE_CANDIDATES = {
    "full": (),
    "no_wifi": ("wifi_",),
    "no_cell": ("cell_",),
    "no_pdr": ("pdr_",),
    "inertial_only": ("wifi_", "cell_", "pdr_"),
    "no_inertial": ("acc_", "gyro_", "mag_"),
}
THRESHOLDS = (0.60, 0.70, 0.80)
PERSISTENCE = (3, 4)


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def endpoint(base_url: str) -> str:
    base = base_url.rstrip("/")
    return base if base.endswith("/chat/completions") else base + "/chat/completions"


def call_agent(url: str, api_key: str, model: str, system: str, prompt: str) -> tuple[str, dict]:
    body = {
        "model": model,
        "temperature": 0.1,
        "messages": [{"role": "system", "content": system}, {"role": "user", "content": prompt}],
        "response_format": {"type": "json_object"},
    }
    request = urllib.request.Request(
        url,
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:1000]
        raise RuntimeError(f"Agent HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Agent network error: {exc.reason}") from exc
    choices = payload.get("choices") or []
    if not choices or not choices[0].get("message", {}).get("content"):
        raise RuntimeError(f"Agent returned no message: {json.dumps(payload)[:1000]}")
    return choices[0]["message"]["content"], payload


def parse_json_object(text: str) -> dict:
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1].rsplit("```", 1)[0]
    value = json.loads(text)
    if not isinstance(value, dict):
        raise ValueError("Agent JSON must be an object")
    return value


def candidate_name(feature_group: str, threshold: float, persistence: int) -> str:
    return f"{feature_group}@{threshold:.2f}x{persistence}"


def evaluate_candidate(rows, names, feature_group: str, threshold: float, persistence: int) -> dict:
    disabled = FEATURE_CANDIDATES[feature_group]
    results = cross_validate(rows, names, disabled, threshold, persistence)
    summary = summarize(results)
    routes = route_holdout(rows, names, disabled, threshold, persistence)
    worst = min(
        (v["leave_detected"] / max(1, v["leave_sessions"]) for v in routes.values()),
        default=0.0,
    )
    return {
        "candidate": candidate_name(feature_group, threshold, persistence),
        "feature_group": feature_group,
        "disabled_prefixes": list(disabled),
        "risk_threshold": threshold,
        "consecutive_ticks": persistence,
        "summary": summary,
        "route_holdout": routes,
        "worst_route_recall": worst,
        "selection_score": candidate_score(summary) + 250.0 * worst,
    }


def compact_profile(rows) -> dict:
    groups = {}
    for row in rows:
        groups.setdefault(row.group_id, {"label": row.label, "route": row.route, "sessions": 0})
        groups[row.group_id]["sessions"] += 1
    sessions = {r.session: r for r in rows}
    return {
        "rows": len(rows),
        "sessions": len(sessions),
        "positive_sessions": sum(r.label == "LEAVE" for r in sessions.values()),
        "negative_sessions": sum(r.label != "LEAVE" for r in sessions.values()),
        "groups": list(groups.values()),
        "feature_names": feature_names(rows),
        "label_definition": "LEAVE means GPS source_type=1 occurs within the prediction horizon; INDOOR_ACTIVITY is a known negative group.",
    }


def make_proposals(env: dict[str, str], profile: dict, out: Path) -> list[dict]:
    system = """You are a constrained machine-learning experiment designer. Propose candidates for a leave-home risk model.
Return JSON only: {\"proposals\":[{\"feature_group\":...,\"risk_threshold\":...,\"consecutive_ticks\":...,\"hypothesis\":...}]}.
Allowed feature_group values: full, no_wifi, no_cell, no_pdr, inertial_only, no_inertial.
Allowed thresholds: 0.60, 0.70, 0.80. Allowed consecutive_ticks: 3 or 4.
Do not invent features, route names, labels, or hard rules. Prefer diverse, testable candidates and explain each hypothesis briefly."""
    prompt = json.dumps({"task": "Generate up to 10 candidates", "profile": profile}, ensure_ascii=False)
    text, raw = call_agent(endpoint(env["SA_AGENT_BASE_URL"]), env["SA_AGENT_API_KEY"], env.get("SA_AGENT_MODEL", "deepseek-chat"), system, prompt)
    (out / "agent_proposal_raw.json").write_text(json.dumps(raw, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    data = parse_json_object(text)
    proposals = data.get("proposals")
    if not isinstance(proposals, list):
        raise ValueError("Agent proposals must be a list")
    accepted = []
    seen = set()
    for item in proposals:
        if not isinstance(item, dict):
            continue
        group = item.get("feature_group")
        threshold = item.get("risk_threshold")
        persistence = item.get("consecutive_ticks")
        if group not in FEATURE_CANDIDATES or threshold not in THRESHOLDS or persistence not in PERSISTENCE:
            continue
        name = candidate_name(group, threshold, persistence)
        if name not in seen:
            seen.add(name)
            accepted.append({"feature_group": group, "risk_threshold": threshold, "consecutive_ticks": persistence, "hypothesis": str(item.get("hypothesis", ""))[:500]})
    return accepted[:10]


def review_results(env: dict[str, str], profile: dict, evaluated: list[dict], out: Path) -> dict:
    safe = [x for x in evaluated if x["summary"]["false_push_groups"] == 0 and x["summary"]["leave_groups_detected"] == x["summary"]["leave_groups"]]
    system = """You are a conservative ML reviewer. Select one candidate only from the supplied evaluated candidates.
Return JSON only: {\"selected_candidate\": string, \"reason\": string, \"reject\": [string]}.
Never select a candidate with false_push_groups > 0 or missed leave groups. Prefer higher worst_route_recall, then higher leave lead.
This is offline evidence, not proof of production generalization."""
    prompt = json.dumps({"task": "Review measured candidates", "profile": profile, "evaluated": evaluated, "safe_candidates": [x["candidate"] for x in safe]}, ensure_ascii=False)
    text, raw = call_agent(endpoint(env["SA_AGENT_BASE_URL"]), env["SA_AGENT_API_KEY"], env.get("SA_AGENT_MODEL", "deepseek-chat"), system, prompt)
    (out / "agent_review_raw.json").write_text(json.dumps(raw, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    review = parse_json_object(text)
    selected = review.get("selected_candidate")
    if selected not in {x["candidate"] for x in safe}:
        raise ValueError(f"Agent selected unsafe or unknown candidate: {selected}")
    return review


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", default=r"D:\huawei\data\0811")
    ap.add_argument("--groups", default=str(ROOT / "config" / "0811_event_groups.json"))
    ap.add_argument("--fingerprint", default=str(ROOT / "config" / "company_radio_fingerprint.json"))
    ap.add_argument("--env-file", default=str(ROOT / "sa_service" / "etc" / "agent.env"))
    ap.add_argument("--out", default=str(ROOT / "output" / "agent_leave_risk_v1"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    env = read_env(Path(args.env_file))
    if not env.get("SA_AGENT_BASE_URL") or not env.get("SA_AGENT_API_KEY") or env["SA_AGENT_API_KEY"].startswith("YOUR_"):
        raise RuntimeError("Missing usable SA_AGENT_BASE_URL/SA_AGENT_API_KEY")
    rows = load_event_rows(Path(args.data_root), Path(args.groups), Path(args.fingerprint))
    names = feature_names(rows)
    profile = compact_profile(rows)
    (out / "dataset_profile.json").write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    proposals = make_proposals(env, profile, out)
    if not proposals:
        raise RuntimeError("Agent returned no valid proposals")
    evaluated = [evaluate_candidate(rows, names, p["feature_group"], p["risk_threshold"], p["consecutive_ticks"]) for p in proposals]
    for result, proposal in zip(evaluated, proposals):
        result["hypothesis"] = proposal["hypothesis"]
    (out / "evaluated_candidates.json").write_text(json.dumps(evaluated, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    review = review_results(env, profile, evaluated, out)
    (out / "agent_review.json").write_text(json.dumps(review, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    chosen = next(x for x in evaluated if x["candidate"] == review["selected_candidate"])
    disabled = tuple(chosen["disabled_prefixes"])
    active, models = fit_models(rows, names, disabled)
    serialize_model(out / "leave_risk_model.json", rows, active, models, {
        "method": "remote_agent_proposal+local_grouped_cv+remote_agent_review",
        "selected_candidate": chosen["candidate"],
        "agent_reason": review.get("reason", ""),
        "summary": chosen["summary"],
        "route_holdout": chosen["route_holdout"],
        "risk_threshold": chosen["risk_threshold"],
        "consecutive_ticks": chosen["consecutive_ticks"],
        "generated_at": datetime.now(timezone.utc).isoformat(),
    })
    print(json.dumps({"selected": chosen["candidate"], "reason": review.get("reason", ""), "summary": chosen["summary"]}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
