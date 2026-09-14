"""Real-history module isolation regression; writes artifacts only to --output."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--source", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--old-binary", type=Path, required=True)
a = p.parse_args()
shutil.copytree(a.source, a.output)
def call(binary, command, params):
    r = subprocess.run([str(binary.resolve()), str(a.output.resolve()), command,
                        json.dumps(params)], check=True, capture_output=True, text=True)
    return json.loads(r.stdout)
old = call(a.old_binary, "template_evaluate_frozen", {"include_prefix_trace": False})
new = call(a.binary, "template_evaluate_frozen", {"include_prefix_trace": False})
assert old["frozen"]["episode_results"] == new["frozen"]["episode_results"]
active = json.loads((a.output / "active_context_template.json").read_text())
# Preserve the frozen profile; train from the unpersonalized baseline in this copy.
(a.output / "active_context_template.json").rename(a.output / "frozen_reference.json")
proposal = {k: active[k] for k in ("anchor_id", "positive_sequence", "applicability")}
proposal.update(template_name="module_isolation_test", readiness_policy="disambiguate",
                cancel_paths="baro_ascending,vertical_closure|geo_outbound,approaching,attached",
                rationale="Developer-controlled regression, not an LLM discovery.")
result = call(a.binary, "template_fit", proposal)
(a.output / "module_search_test.json").write_text(json.dumps(result, indent=2))
t = result["trial"]
assert t["ok"] and len(t["module_catalog"]) == 3
assert {c["combination_mask"] for c in t["candidates"]} == set(range(8))
assert any("requires_2" in c["rejection"] for c in t["candidates"])
assert any(c["metrics"].get("mean_lead_s", -1) >= 0 for c in t["candidates"])
assert t["best_candidate_id"] is not None
selected = next(c for c in t["candidates"] if c["id"] == t["best_candidate_id"])
assert selected["eligible"]
after = call(a.binary, "template_evaluate_frozen", {"include_prefix_trace": False})
(a.output / "after.json").write_text(json.dumps(after, indent=2))
print(json.dumps({"evaluated": t["evaluated_candidate_count"], "shortlist": len(t["candidates"]),
                  "selected_mask": selected["combination_mask"], "commit": result["commit"],
                  "before": {k: new["frozen"].get(k) for k in
                    ("false_kept", "missed_leave", "mean_lead_s", "aborted_visible_push")},
                  "after": {k: after["frozen"].get(k) for k in
                    ("false_kept", "missed_leave", "mean_lead_s", "aborted_visible_push")}}, indent=2))
