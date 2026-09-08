"""Build a deduplicated 0812/0813/0814 product root for cancel-sequence learning.

The source policy histories must come from ``validate_raw_frozen_templates.py``
with the current observation schema.  One representative is retained for each
synchronous multi-device collection group so shared barometer data is not
mistaken for independent behavioral evidence.

Labels in this experiment are deliberately separate from the raw replay:
0812/0813 reuse the project's existing reviewed episode labels; the three 0814
negative collection processes reuse the user's explicit annotation.  C++ still
has to validate a shared confirmed prefix, ascent, vertical closure, and no
outside before an Agent proposal becomes ABORTED_LEAVE.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# (source directory, representative episode, reviewed label)
EPISODES = (
    ("cancel_multiday_0812", "20260812_105415", "FALSE_PUSH"),
    ("cancel_multiday_0812", "20260812_110128", "FALSE_PUSH"),
    ("cancel_multiday_0812", "20260812_111502", "FALSE_PUSH"),
    ("cancel_multiday_0812", "20260812_152909", "FALSE_PUSH"),
    ("cancel_multiday_0812", "20260812_153840", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_151637", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_152636", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_153323", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_155720", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_160252", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_160817", "CONFIRMED_LEAVE"),
    ("cancel_multiday_0813", "20260813_162852", "FALSE_PUSH"),
    ("cancel_multiday_0813", "20260813_163747", "FALSE_PUSH"),
    ("cancel_multiday_0813", "20260813_164702", "FALSE_PUSH"),
    ("cancel_multiday_0813", "20260813_165122", "FALSE_PUSH"),
    ("cancel_multiday_0813", "20260813_165548", "FALSE_PUSH"),
    ("cancel_multiday_0813", "20260813_165944", "FALSE_PUSH"),
    ("aborted_leave_0814_regression", "20260814_170055", "FALSE_PUSH"),
    ("aborted_leave_0814_regression", "20260814_170939", "FALSE_PUSH"),
    ("aborted_leave_0814_regression", "20260814_171505", "FALSE_PUSH"),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path,
                        default=ROOT / "output" / "cancel_sequence_multiday_agent")
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit(f"output already exists: {out}")
    out.mkdir(parents=True)

    selected = {(source, episode): label for source, episode, label in EPISODES}
    rows: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for source in dict.fromkeys(source for source, _, _ in EPISODES):
        source_path = ROOT / "output" / source / "frozen" / "policy_history.jsonl"
        if not source_path.is_file():
            raise SystemExit(f"missing source history: {source_path}")
        for line in source_path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            episode = row.get("episode_id", "")
            key = (source, episode)
            if key not in selected:
                continue
            row["label"] = selected[key]
            rows.append(row)
            seen.add(key)

    tick_keys = [(r['episode_id'], r['outcome_t_ms'], r['side'], r['t_ms']) for r in rows]
    if len(tick_keys) != len(set(tick_keys)):
        raise SystemExit('duplicate episode/timestamp in source history; resolve before training')

    missing = set(selected) - seen
    if missing:
        raise SystemExit(f"episodes absent from source histories: {sorted(missing)}")
    (out / "policy_history.jsonl").write_text(
        "".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows), encoding="utf-8")

    # The host requires the product files. Evidence and evaluation are sourced
    # from policy_history; these label records make the reviewed provenance
    # visible to the Agent without fabricating push timestamps.
    label_lines = []
    for source, episode, label in EPISODES:
        episode_rows = [r for r in rows if r.get("episode_id") == episode]
        outcome = int(episode_rows[0]["outcome_t_ms"])
        label_lines.append(json.dumps({"type": "label", "t_label_ms": outcome,
            "outcome_t_ms": outcome, "label": label, "side": "company",
            "episode_id": episode, "truth_source": "REVIEWED_MULTIDAY_EXPERIMENT"}))
    (out / "leave_episodes.jsonl").write_text("\n".join(label_lines) + "\n", encoding="utf-8")

    shutil.copy2(ROOT / "config" / "theta_default.json", out / "theta.json")
    shutil.copy2(ROOT / "config" / "anchors.json", out / "anchors.json")
    profile = ROOT / "output" / "prefix_lead_final" / "train" / "active_context_template.json"
    if profile.is_file():
        shutil.copy2(profile, out / "active_context_template.json")
    for name in ("episode_interpretations.jsonl", "context_templates.jsonl", "audit.jsonl",
                 "param_changes.jsonl", "personalize_jobs.jsonl"):
        (out / name).write_text("", encoding="utf-8")
    (out / "dataset_manifest.json").write_text(json.dumps({
        "deduplication": "one representative per synchronous collection group",
        "sources": sorted({source for source, _, _ in EPISODES}),
        "episodes": [{"source": s, "episode_id": e, "label": label}
                     for s, e, label in EPISODES],
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"ok": True, "output": str(out), "episodes": len(EPISODES),
                      "ticks": len(rows)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
