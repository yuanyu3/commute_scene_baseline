#!/usr/bin/env python3
"""Train/evaluate multi-horizon leave risk without route-specific features."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.hsmm import LeaveHsmm, LeaveObservation, LeavePhase  # noqa: E402
from commute_baseline.leave_risk import (  # noqa: E402
    HORIZONS_S, RiskRow, feature_names, label_for, load_event_rows,
)


def matrix(rows, names):
    return np.asarray([[r.features.get(n, 0.0) for n in names] for r in rows], dtype=float)


def fit_models(rows, names, disabled=()):
    active = [n for n in names if not any(n.startswith(prefix) for prefix in disabled)]
    x = matrix(rows, active)
    models = {}
    for horizon in HORIZONS_S:
        y = np.asarray([label_for(r, horizon) for r in rows])
        group_sizes = defaultdict(int)
        for row in rows:
            group_sizes[row.group_id] += 1
        sample_weight = np.asarray([1.0 / group_sizes[row.group_id] for row in rows], dtype=float)
        class_mass = {target: sample_weight[y == target].sum() for target in (0, 1)}
        for target in (0, 1):
            if class_mass[target] > 0:
                sample_weight[y == target] *= 0.5 / class_mass[target]
        sample_weight *= len(rows) / max(1e-9, sample_weight.sum())
        # Strong class balancing is needed because only the pre-outdoor tail is positive.
        model = Pipeline([
            ("impute", SimpleImputer(strategy="constant", fill_value=0.0)),
            ("scale", StandardScaler()),
            ("clf", LogisticRegression(C=0.20, max_iter=2000, solver="liblinear")),
        ])
        model.fit(x, y, clf__sample_weight=sample_weight)
        models[horizon] = model
    return active, models


def predict(rows, names, models):
    x = matrix(rows, names)
    return {h: models[h].predict_proba(x)[:, 1] for h in HORIZONS_S}


def first_trigger(rows: list[RiskRow], risks, threshold=0.60, consecutive=3):
    hsmm = LeaveHsmm()
    streak = 0
    for i, row in enumerate(rows):
        obs = LeaveObservation(
            walking=row.features.get("walking", 0.0),
            risk_available=True,
            risk_30s=float(risks[30][i]),
            risk_60s=float(risks[60][i]),
            risk_120s=float(risks[120][i]),
            relation_known=True,
            inside=True,
        )
        result = hsmm.step(obs, datetime.fromtimestamp(row.t_ms / 1000, tz=timezone.utc), {"w_risk": 1.2})
        smooth = result.probability[LeavePhase.PRE_LEAVE] + result.probability[LeavePhase.LEAVING]
        eligible = risks[120][i] >= threshold and smooth >= 0.50 and row.features.get("walking", 0) > 0
        streak = streak + 1 if eligible else 0
        if streak >= consecutive:
            return row.t_ms, float(risks[30][i]), float(risks[60][i]), float(risks[120][i]), smooth
    return None


def evaluate_fold(test_rows, names, models, threshold=0.60, consecutive=3):
    by_session = defaultdict(list)
    for r in test_rows:
        by_session[r.session].append(r)
    results = []
    for session, rows in by_session.items():
        rows.sort(key=lambda r: r.t_ms)
        risks = predict(rows, names, models)
        trigger = first_trigger(rows, risks, threshold=threshold, consecutive=consecutive)
        truth = rows[0].truth_t_ms
        results.append({
            "session": session,
            "group_id": rows[0].group_id,
            "label": rows[0].label,
            "route": rows[0].route,
            "trigger_t_ms": trigger[0] if trigger else None,
            "risk_at_trigger": list(trigger[1:4]) if trigger else None,
            "hsmm_risk_at_trigger": trigger[4] if trigger else None,
            "truth_t_ms": truth,
            "lead_s": (truth - trigger[0]) / 1000 if trigger and truth else None,
        })
    return results


def summarize(results):
    positives = [r for r in results if r["label"] == "LEAVE"]
    negatives = [r for r in results if r["label"] != "LEAVE"]
    leads = [r["lead_s"] for r in positives if r["lead_s"] is not None]
    by_group = defaultdict(list)
    for row in results:
        by_group[row["group_id"]].append(row)
    group_rows = []
    for group_id, replicas in by_group.items():
        positive = replicas[0]["label"] == "LEAVE"
        detected = sum(r["trigger_t_ms"] is not None for r in replicas)
        leads_group = [r["lead_s"] for r in replicas if r["lead_s"] is not None]
        group_rows.append({
            "group_id": group_id,
            "label": replicas[0]["label"],
            "route": replicas[0]["route"],
            "replicas": len(replicas),
            "detected_replicas": detected,
            "consistent": detected in (0, len(replicas)),
            "detected": detected > 0 if positive else detected > 0,
            "lead_median_s": float(np.median(leads_group)) if leads_group else None,
        })
    leave_groups = [g for g in group_rows if g["label"] == "LEAVE"]
    indoor_groups = [g for g in group_rows if g["label"] != "LEAVE"]
    return {
        "leave_sessions": len(positives),
        "leave_detected": sum(r["trigger_t_ms"] is not None for r in positives),
        "indoor_sessions": len(negatives),
        "false_pushes": sum(r["trigger_t_ms"] is not None for r in negatives),
        "lead_median_s": float(np.median(leads)) if leads else None,
        "lead_min_s": min(leads) if leads else None,
        "lead_max_s": max(leads) if leads else None,
        "leave_groups": len(leave_groups),
        "leave_groups_detected": sum(g["detected_replicas"] > 0 for g in leave_groups),
        "indoor_groups": len(indoor_groups),
        "false_push_groups": sum(g["detected_replicas"] > 0 for g in indoor_groups),
        "inconsistent_replica_groups": sum(not g["consistent"] for g in group_rows),
        "groups": group_rows,
    }


def candidate_score(summary):
    # Safety dominates timing: one false push or miss costs more than tens of
    # seconds of lead. This is the objective the Agent uses to select a model,
    # not a route-specific rule.
    missed = summary["leave_sessions"] - summary["leave_detected"]
    missed_groups = summary["leave_groups"] - summary["leave_groups_detected"]
    lead = summary["lead_median_s"] or 0.0
    return (
        150.0 * summary["leave_groups_detected"]
        - 600.0 * missed_groups
        - 500.0 * summary["false_push_groups"]
        - 60.0 * missed
        - 80.0 * summary["inconsistent_replica_groups"]
        + min(lead, 120.0)
    )


def cross_validate(rows, all_names, disabled=(), threshold=0.60, consecutive=3):
    groups = sorted({r.group_id for r in rows})
    results = []
    for held in groups:
        train = [r for r in rows if r.group_id != held]
        test = [r for r in rows if r.group_id == held]
        active, models = fit_models(train, all_names, disabled)
        results.extend(evaluate_fold(test, active, models, threshold, consecutive))
    return results


def route_holdout(rows, all_names, disabled=(), threshold=0.60, consecutive=3):
    """Train without each positive route family, while retaining negatives."""
    reports = {}
    routes = sorted({r.route for r in rows if r.label == "LEAVE"})
    for route in routes:
        train = [r for r in rows if r.label != "LEAVE" or r.route != route]
        test = [r for r in rows if r.label == "LEAVE" and r.route == route]
        # A route holdout is only meaningful when both classes remain.
        if not train or not test or len({label_for(r, 120) for r in train}) < 2:
            continue
        active, models = fit_models(train, all_names, disabled)
        reports[route] = summarize(evaluate_fold(test, active, models, threshold, consecutive))
    return reports


def serialize_model(path, rows, names, models, report):
    body = {"schema_version": 1, "model": "multi_horizon_logistic", "features": names,
            "horizons_s": list(HORIZONS_S), "trained_at": datetime.now(timezone.utc).isoformat(),
            "training_groups": sorted({r.group_id for r in rows}), "report": report, "heads": {}}
    for h, pipe in models.items():
        scale = pipe.named_steps["scale"]
        clf = pipe.named_steps["clf"]
        body["heads"][str(h)] = {"mean": scale.mean_.tolist(), "scale": scale.scale_.tolist(),
                                  "coef": clf.coef_[0].tolist(), "intercept": float(clf.intercept_[0])}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", default=r"D:\huawei\data\0811")
    ap.add_argument("--groups", default=str(ROOT / "config" / "0811_event_groups.json"))
    ap.add_argument("--fingerprint", default=str(ROOT / "config" / "company_radio_fingerprint.json"))
    ap.add_argument("--out", default=str(ROOT / "output" / "leave_risk_v1"))
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    rows = load_event_rows(Path(args.data_root), Path(args.groups), Path(args.fingerprint))
    names = feature_names(rows)
    groups = sorted({r.group_id for r in rows})
    feature_candidates = {
        "full": (),
        "no_wifi": ("wifi_",),
        "no_cell": ("cell_",),
        "no_pdr": ("pdr_",),
        "inertial_only": ("wifi_", "cell_", "pdr_"),
        "no_inertial": ("acc_", "gyro_", "mag_"),
    }
    candidate_reports = {}
    candidate_results = {}
    for feature_name, disabled in feature_candidates.items():
        for threshold in (0.60, 0.70, 0.80):
            for consecutive in (3, 4):
                candidate_name = f"{feature_name}@{threshold:.2f}x{consecutive}"
                results = cross_validate(rows, names, disabled, threshold, consecutive)
                summary = summarize(results)
                route_reports = route_holdout(rows, names, disabled, threshold, consecutive)
                worst_route_recall = min(
                    (v["leave_detected"] / max(1, v["leave_sessions"]) for v in route_reports.values()),
                    default=0.0,
                )
                score = candidate_score(summary) + 250.0 * worst_route_recall
                candidate_reports[candidate_name] = {
                    "feature_candidate": feature_name,
                    "disabled_prefixes": list(disabled),
                    "risk_threshold": threshold,
                    "consecutive_ticks": consecutive,
                    "summary": summary,
                    "route_holdout": route_reports,
                    "worst_route_recall": worst_route_recall,
                    "selection_score": score,
                }
                candidate_results[candidate_name] = results
    # Deterministic tie break prefers fewer dependencies, then lexical name.
    selected = max(
        candidate_reports,
        key=lambda n: (
            candidate_reports[n]["selection_score"],
            len(candidate_reports[n]["disabled_prefixes"]),
            n,
        ),
    )
    fold_results = candidate_results[selected]
    report = {
        "method": "agent_style_feature_group_search+leave_one_event_group_out+route_holdout",
        "n_rows": len(rows), "n_groups": len(groups), "selected_candidate": selected,
        "summary": summarize(fold_results), "sessions": fold_results,
        "candidates": candidate_reports,
    }
    selected_config = candidate_reports[selected]
    active, models = fit_models(rows, names, tuple(selected_config["disabled_prefixes"]))
    serialize_model(out / "leave_risk_model.json", rows, active, models, {
        **report["summary"],
        "selected_candidate": selected,
        "risk_threshold": selected_config["risk_threshold"],
        "consecutive_ticks": selected_config["consecutive_ticks"],
    })
    (out / "evaluation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], ensure_ascii=False, indent=2))
    for r in fold_results:
        print(r)
    print("selected", selected)
    print("candidates", json.dumps(candidate_reports, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
