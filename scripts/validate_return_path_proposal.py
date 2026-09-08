"""Revalidate saved Agent structure with the current deterministic tool build."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--proposal', type=Path, required=True)
    p.add_argument('--interpretations', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    for name in ['policy_history.jsonl', 'theta.json', 'anchors.json', 'active_context_template.json']:
        shutil.copy2(a.source / name, a.output / name)
    shutil.copy2(a.interpretations, a.output / 'episode_interpretations.jsonl')
    proposal = json.loads(a.proposal.read_text())
    allowed = ['template_name', 'side', 'anchor_id', 'applicability', 'positive_sequence',
               'cancel_sequence', 'cancel_paths', 'negative_pattern', 'parameter_families', 'rationale']
    structure = {key: proposal[key] for key in allowed if key in proposal}
    # Saved Agent numeric values are intentionally excluded; current C++ fits them again.
    result = subprocess.run([str(a.binary.resolve()), str(a.output.resolve()), 'template_fit',
        json.dumps(structure)], text=True, capture_output=True, check=True)
    data = json.loads(result.stdout)
    (a.output / 'revalidation.json').write_text(json.dumps(data, indent=2))
    print(json.dumps({'trial_ok': data['trial'].get('ok'), 'commit': data['commit'],
                     'normalized_template': data['trial'].get('generated_template')}))
    return 0 if data['commit'].get('ok') else 1


if __name__ == '__main__':
    raise SystemExit(main())
