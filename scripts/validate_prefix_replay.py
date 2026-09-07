"""Frozen train/validation experiment. Run in WSL; outputs stay local, sources unchanged."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--train', required=True, type=Path)
    parser.add_argument('--validation', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)

    def call(root, command, params=None):
        run = subprocess.run([str(args.binary.resolve()), str(root.resolve()), command,
                              json.dumps(params or {})], check=True, capture_output=True, text=True)
        return json.loads(run.stdout)

    roots = {}
    for name, source in [('train', args.train), ('validation', args.validation)]:
        root = args.output / name
        root.mkdir()
        for filename in ['policy_history.jsonl', 'theta.json', 'active_context_template.json']:
            shutil.copy2(source / filename, root / filename)
        roots[name] = root
    # Compare identical training-derived template structure on both days.
    old_profile = json.loads((roots['train'] / 'active_context_template.json').read_text())
    (roots['validation'] / 'active_context_template.json').write_text(json.dumps(old_profile))
    before = {name: call(root, 'template_evaluate_frozen') for name, root in roots.items()}
    fit = call(roots['train'], 'template_fit', old_profile)
    if not fit['commit'].get('ok'):
        raise RuntimeError(fit)
    shutil.copy2(roots['train'] / 'active_context_template.json',
                 roots['validation'] / 'active_context_template.json')
    after = {name: call(root, 'template_evaluate_frozen', {'include_prefix_trace': True})
             for name, root in roots.items()}
    # Removing the suffix must not change any earlier posterior or push decision.
    for name, root in roots.items():
        episodes = after[name]['frozen']['episode_results']
        episode = next(ep for ep in episodes if ep['would_push'])
        cutoff = episode['push_t_ms']
        short = call(root, 'template_evaluate_frozen',
                     {'include_prefix_trace': True, 'cutoff_t_ms': cutoff})['frozen']
        for full, partial in zip(episodes, short['episode_results']):
            expected = [tick for tick in full['prefix_trace'] if tick['t_ms'] <= cutoff]
            assert partial['prefix_trace'] == expected, (name, 'non-causal prefix')
    report = {'fit': fit, 'before': before, 'after': after, 'prefix_invariance': True}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    for name in roots:
        a, b = before[name]['frozen'], after[name]['frozen']
        print(name, {key: [a[key], b[key]] for key in
                     ['mean_lead_s', 'late_seconds', 'false_kept', 'missed_leave', 'score']})
        for x, y in zip(a['episode_results'], b['episode_results']):
            print(x['label'], x['lead_s'], '->', y['lead_s'], 'push_ms', y['push_t_ms'])
    print('selected', after['train']['template'])
    print('prefix invariance: PASS')


if __name__ == '__main__':
    main()
