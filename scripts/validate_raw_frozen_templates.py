"""Replay a new collection day without tuning on validation.

Uses the existing Python feature extractor, then the canonical C++ HSMM/template.
Sessions stay separate; GPS transition labels are proxies, others remain UNLABELED.
Raw GPS/radio and detailed outputs are local only. No LLM/network calls.
"""
import argparse
import csv
import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plan_baro_groups(sessions, share=False, gap_s=60):
    """User-authorized synchronous triplets only; do not chain near groups.

    Folder time is a grouping key, NOT a sensor clock offset. The extractor uses
    original wallTsMs, consuming samples only once their timestamp is reached.
    """
    counts = {}
    for session in sessions:
        counts[session] = 0
        for path in session.glob('baro_data_*.csv'):
            with path.open(encoding='utf-8-sig') as stream:
                counts[session] += max(0, sum(1 for _ in csv.reader(stream)) - 1)
    groups = []
    for session in sorted(sessions):
        time = datetime.strptime(session.name, '%Y%m%d_%H%M%S')
        if not groups or (time - groups[-1][0]).total_seconds() > gap_s:
            groups.append((time, []))
        groups[-1][1].append(session)
    plan = {}
    for group_index, (_, group) in enumerate(groups):
        donors = [p for p in group if counts[p] >= 50]
        for session in group:
            donor = session
            # No automatic sharing if grouping is ambiguous (not a triplet).
            if share and len(group) == 3 and counts[session] < 50 and donors:
                donor = min(donors, key=lambda p: (abs((datetime.strptime(p.name, '%Y%m%d_%H%M%S') -
                    datetime.strptime(session.name, '%Y%m%d_%H%M%S')).total_seconds()), p.name))
            plan[session] = {'group_id': group_index + 1, 'group_size': len(group),
                'own_baro_rows': counts[session], 'baro_source': donor.name,
                'source_baro_rows': counts[donor], 'baro_shared': donor != session}
    return plan


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--profile', type=Path, required=True)
    ap.add_argument('--theta', type=Path, required=True)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--share-group-baro', action='store_true',
                    help='User-authorized synchronous triplets may share baro when own rows <50')
    ap.add_argument('--group-gap-s', type=float, default=60,
                    help='Maximum span from first to last triplet directory timestamp (1..60)')
    args = ap.parse_args()
    if not 1 <= args.group_gap_s <= 60:
        ap.error('--group-gap-s must be in 1..60')
    args.output.mkdir(parents=True, exist_ok=False)
    frozen = args.output / 'frozen'
    frozen.mkdir()
    shutil.copy2(args.profile, frozen / 'active_context_template.json')
    shutil.copy2(args.theta, frozen / 'theta.json')
    sessions = [p for p in sorted(args.data.iterdir()) if p.is_dir() and (p / 'sensor_events.csv').is_file()]
    baro_plan = plan_baro_groups(sessions, args.share_group_baro, args.group_gap_s)
    metadata = {'profile_sha256': sha(args.profile), 'theta_sha256': sha(args.theta),
                'sensor_borrowing': args.share_group_baro, 'group_gap_s': args.group_gap_s,
                'n_collection_groups': len({p['group_id'] for p in baro_plan.values()}), 'sessions': []}
    all_rows = []
    for raw in sessions:
        session_out = args.output / raw.name
        command = [sys.executable, str(ROOT / 'examples/run_real_flow.py'),
            '--raw-dir', str(raw), '--sensor-dir', str(raw), '--out-dir', str(session_out),
            '--anchors', str(ROOT / 'config/anchors.json'), '--theta', str(args.theta),
            '--company-radio-fingerprint', str(ROOT / 'config/company_radio_fingerprint.json'),
            '--location-crs', 'GCJ02', '--tick-s', '5', '--episode-truth', 'auto', '--stop-at-motion', '--local-time-axis']
        if baro_plan[raw]['baro_shared']:
            command.extend(['--baro-dir', str(args.data / baro_plan[raw]['baro_source'])])
        run = subprocess.run(command, capture_output=True, text=True)
        (args.output / (raw.name + '.log')).write_text(run.stdout + run.stderr)
        info = {'session': raw.name, 'returncode': run.returncode, **baro_plan[raw]}
        metadata['sessions'].append(info)
        if run.returncode:
            print(raw.name, 'FAILED; see local log', flush=True)
            continue
        data = json.loads((session_out / 'replay_decisions.json').read_text())
        decisions = data['decisions']
        if not decisions:
            info['status'] = 'empty'
            continue
        # Do not use model push/absence of GPS or baro to construct truth labels.
        events = []
        previous = 'UNKNOWN'
        for r in decisions:
            relation = r['company_relation']
            ev = r.get('evidence', {}).get('company', {})
            if (previous in ('INSIDE', 'NEAR') and relation == 'OUTSIDE'
                    and r.get('gps_source_type') == 1 and not ev.get('approaching', False)):
                events.append(r['t_ms'])
            if relation != 'UNKNOWN':
                previous = relation
        outcome = events[0] if len(events) == 1 else decisions[-1]['t_ms']
        label = 'CONFIRMED_LEAVE' if len(events) == 1 else 'UNLABELED'
        info.update(label=label, label_source='GPS_RELATION_PROXY' if len(events) == 1 else 'UNKNOWN',
                    outward_events=len(events))
        n = 0
        for r in decisions:
            if r['t_ms'] > outcome:
                break
            obs = r.get('evidence', {}).get('company', {}).get('obs', {})
            if 'pdr_outbound' not in obs:
                raise RuntimeError('Missing atomic HSMM observation; cannot fabricate evidence')
            row = {'episode_id': raw.name, 't_ms': r['t_ms'], 'outcome_t_ms': outcome,
                   'side': 'company', 'label': label,
                   'baro_descent_m': r.get('baro_descent_m', 0),
                   'baro_stable_platform': r.get('baro_stable_platform', False)}
            row.update({'obs_' + key: value for key, value in obs.items()})
            all_rows.append(row)
            n += 1
        info['ticks'] = n
        print(raw.name, label, 'baro_source', info['baro_source'], 'ticks', n, flush=True)
    (frozen / 'policy_history.jsonl').write_text('\n'.join(json.dumps(row) for row in all_rows))
    if not all_rows:
        raise RuntimeError('No usable observations; check local logs')

    def call(root, command, params):
        result = subprocess.run([str(args.binary.resolve()), str(root.resolve()), command,
            json.dumps(params)], check=True, capture_output=True, text=True)
        return json.loads(result.stdout)

    result = call(frozen, 'template_evaluate_frozen', {})
    diagnostics = {arm: call(frozen, 'template_diagnose', {'ablation': arm, 'limit': 100})
                   for arm in ['positive', 'negative', 'both']}
    assert sha(args.profile) == metadata['profile_sha256']
    assert sha(args.theta) == metadata['theta_sha256']
    report = {'provenance': metadata, 'evaluation': result, 'diagnostics': diagnostics}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    for arm in ['baseline', 'frozen']:
        m = result[arm]
        print(arm, {k: m[k] for k in ['n_episodes', 'n_confirmed_leave', 'unscored', 'mean_lead_s', 'missed_leave']})
    print('NOTE: UNLABELED pushes cannot be counted as correct/false; GPS positives are proxies.')


if __name__ == '__main__':
    main()
