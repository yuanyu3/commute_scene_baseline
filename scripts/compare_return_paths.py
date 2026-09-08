"""Frozen old/new C++ comparison; original outcome labels remain the scoring truth.

Run under WSL/Linux. Copies profiles/data into a new local output folder. Does
not fit parameters or call a model. Agent interpretations are held equal in
both arms, and never replace raw labels for the accuracy counters below.
"""
import argparse
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--old-profile', type=Path, required=True)
    p.add_argument('--new-profile', type=Path, required=True)
    p.add_argument('--old-binary', type=Path, required=True)
    p.add_argument('--new-binary', type=Path, required=True)
    p.add_argument('--interpretations', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    labels = {}
    for line in (a.source / 'policy_history.jsonl').read_text().splitlines():
        row = json.loads(line)
        key = (row['episode_id'], row['outcome_t_ms'], row['side'])
        labels[key] = row['label']
    results = {}
    for arm, binary, profile in [('before', a.old_binary, a.old_profile),
                                 ('after', a.new_binary, a.new_profile)]:
        dest = a.output / arm
        dest.mkdir()
        for name in ['policy_history.jsonl', 'theta.json']:
            shutil.copy2(a.source / name, dest / name)
        shutil.copy2(profile, dest / 'active_context_template.json')
        if a.interpretations:
            shutil.copy2(a.interpretations, dest / 'episode_interpretations.jsonl')
        run = subprocess.run([str(binary.resolve()), str(dest.resolve()),
            'template_evaluate_frozen', '{}'], check=True, text=True, capture_output=True)
        result = json.loads(run.stdout)
        if not result.get('ok'):
            raise RuntimeError(result)
        results[arm] = result
        (dest / 'evaluation.json').write_text(json.dumps(result, indent=2))
    metrics, episodes = {}, {}
    for arm, result in results.items():
        rows = result['frozen']['episode_results']
        m = dict(tp=0, fn=0, fp=0, tn=0, unscored=0, cancel_matches=0)
        episodes[arm] = {}
        for row in rows:
            key = (row['episode_id'], row['outcome_t_ms'], row['side'])
            label = labels[key]
            positive = label in ('CONFIRMED_LEAVE', 'MISSED_LEAVE')
            negative = label in ('FALSE_PUSH', 'TRUE_NEGATIVE')
            if positive:
                m['tp' if row['would_push'] else 'fn'] += 1
            elif negative:
                m['fp' if row['would_push'] else 'tn'] += 1
            else:
                m['unscored'] += 1
            m['cancel_matches'] += bool(row['first_cancel_t_ms'])
            episodes[arm][key] = row
        n = sum(m[k] for k in ('tp', 'fn', 'fp', 'tn'))
        m['accuracy'] = (m['tp'] + m['tn']) / n if n else None
        metrics[arm] = m
    comparisons = []
    for key, before in episodes['before'].items():
        after = episodes['after'][key]
        comparisons.append({'episode_id': key[0], 'original_label': labels[key],
            'before_push_t_ms': before['push_t_ms'], 'after_push_t_ms': after['push_t_ms'],
            'push_delay_change_s': (after['push_t_ms'] - before['push_t_ms']) / 1000
                if before['would_push'] and after['would_push'] else None,
            'before_cancel_t_ms': before['first_cancel_t_ms'],
            'after_cancel_t_ms': after['first_cancel_t_ms']})
    report = {'metrics': metrics, 'episodes': comparisons,
        'scope': 'Frozen replay, original labels for accuracy; same interpretations in both arms; not held-out generalization.'}
    (a.output / 'comparison.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(metrics), flush=True)
    for row in comparisons:
        if row['before_push_t_ms'] or row['after_push_t_ms']:
            def time(t):
                return datetime.fromtimestamp(t / 1000, timezone(timedelta(hours=8))).isoformat() if t else '-'
            print(row['episode_id'], time(row['before_push_t_ms']), time(row['after_push_t_ms']))


if __name__ == '__main__':
    main()
