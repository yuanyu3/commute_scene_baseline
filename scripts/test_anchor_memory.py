"""Cross-process memory persistence, anchor isolation and revision regression."""
import json
import subprocess
import tempfile
from pathlib import Path
import sys

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="anchor-memory-") as directory:
    root = Path(directory)
    (root / "anchors.json").write_text(json.dumps({
        "home": {"id": "home_001", "lat": 0, "lon": 0},
        "company": {"id": "company_001", "lat": 1, "lon": 1}}))
    (root / "policy_history.jsonl").write_text(
        '{"anchor_id":"company_001","episode_id":"sample-a","label":"FALSE_PUSH"}\n'
        '{"anchor_id":"home_001","episode_id":"sample-b","label":"CONFIRMED_LEAVE"}\n')
    (root / "audit.jsonl").write_text(
        '{"audit_id":"audit-a","changes":{"anchor_id":"company_001"}}\n')
    def call(command, **params):
        return json.loads(subprocess.check_output(
            [binary, directory, command, json.dumps(params, ensure_ascii=False)], text=True))
    args = dict(anchor_id="company_001", memory_id="height", expected_revision=0,
                claim='高度 "hypothesis"', status="hypothesis", applicability="baro valid",
                limitations="single capture; unverified", support_episode_ids="sample-a")
    assert call("memory_get", anchor_id="company_001")["total"] == 0
    assert not call("memory_update", **dict(args, support_episode_ids="sample-b"))["ok"]
    first = call("memory_update", **args)
    assert first["ok"], first
    assert first["memory"]["claim"] == args["claim"]
    assert not call("memory_update", **args)["ok"], "stale revision accepted"
    assert call("memory_get", anchor_id="home_001")["total"] == 0
    assert call("memory_get", anchor_id="company_001", query="高度")["total"] == 1
    update = dict(args, expected_revision=1, status="supported")
    assert not call("memory_update", **update)["ok"], "audit required"
    result = call("memory_update", **dict(update, audit_id="audit-a"))
    assert result["ok"], result
    assert result["memory"]["revision"] == 2
    assert not call("memory_update", **dict(args, expected_revision=2, status="contested"))["ok"]
    result = call("memory_update", **dict(args, expected_revision=2, status="contested",
                                        counterexample_episode_ids="sample-a"))
    assert result["ok"]
    result = call("memory_update", **dict(args, expected_revision=3, status="retired"))
    assert result["ok"]
    assert call("memory_get", anchor_id="company_001")["total"] == 0
    assert len((root / "anchor_memory.jsonl").read_text().splitlines()) == 4
    assert not (root / "active_context_template.json").exists()
print("PASS persistence across processes, UTF-8/quotes, isolation, references, revisions, retirement")
