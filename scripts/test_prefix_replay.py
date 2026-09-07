"""Synthetic regressions against the real C++ replay CLI (no personal data)."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='commute-prefix-') as directory:
    root = Path(directory)
    (root / 'theta.json').write_text(json.dumps({'enter_leave': 0.58, 'arm_delay_s': 0,
        'lead_min_s': 90, 'lead_max_s': 180, 'focus_side': 'company'}))
    (root / 'active_context_template.json').write_text(json.dumps({
        'schema_version': 4, 'template_name': 'synthetic', 'side': 'company',
        'anchor_id': 'synthetic', 'applicability': 'always',
        'positive_sequence': 'wifi_detach,pdr_outbound,geo_outbound',
        'ready_prefix_length': 2, 'strength': 0.4}))

    def replay(outcome, label='CONFIRMED_LEAVE', cutoff=0):
        rows = []
        for i in range(60):
            rows.append({'t_ms': 1000000 + i * 5000, 'outcome_t_ms': outcome,
                'side': 'company', 'label': label, 'obs_walking': 1,
                'obs_pdr_outbound': 1, 'obs_wifi_detach': 1, 'obs_geo_outbound': 1,
                'obs_inside': True, 'obs_relation_known': True,
                'obs_baro_lower_platform': 1, 'obs_baro_available': True})
        (root / 'policy_history.jsonl').write_text('\n'.join(json.dumps(r) for r in rows))
        params = {'include_prefix_trace': True, 'cutoff_t_ms': cutoff}
        result = subprocess.run([binary, directory, 'template_evaluate_frozen', json.dumps(params)],
                                check=True, capture_output=True, text=True)
        return json.loads(result.stdout)['frozen']

    initial = replay(1400000)
    push = initial['episode_results'][0]['push_t_ms']
    assert push > 0
    late = replay(push + 20000)
    earlier = replay(push + 75000)
    assert abs(earlier['score'] - late['score'] - 55 / 60) < 1e-4
    soft = replay(push + 75000, 'FALSE_PUSH')
    assert soft['mean_lead_s'] == 75 and soft['late_seconds'] == 15
    # Changing outcome time and label must not affect inference.
    assert soft['episode_results'][0]['prefix_trace'] == initial['episode_results'][0]['prefix_trace']
    partial = replay(1400000, cutoff=push)
    assert partial['partial_replay']
    assert partial['episode_results'][0]['prefix_trace'] == [
        t for t in initial['episode_results'][0]['prefix_trace'] if t['t_ms'] <= push]
    print('PASS: continuous timing, soft-positive timing, label/time independence, prefix causality')
    profile_before = (root / 'active_context_template.json').read_bytes()
    theta_before = (root / 'theta.json').read_bytes()
    for ablation in ['positive', 'negative', 'both']:
        result = subprocess.run([binary, directory, 'template_diagnose',
            json.dumps({'ablation': ablation})], check=True, capture_output=True, text=True)
        diagnosis = json.loads(result.stdout)
        assert diagnosis['ok'] and diagnosis['read_only']
        row = diagnosis['active']['episode_results'][0]
        assert row['first_ready_t_ms'] > 0 and row['first_complete_t_ms'] > row['first_ready_t_ms']
        if ablation == 'negative':  # No negative clause in this synthetic template.
            assert diagnosis['active'] == diagnosis['ablated']
    assert (root / 'active_context_template.json').read_bytes() == profile_before
    assert (root / 'theta.json').read_bytes() == theta_before
    print('PASS: diagnostic ablation, semantic timestamps, profile/theta immutability')
    rows = [json.loads(line) for line in (root / 'policy_history.jsonl').read_text().splitlines()]
    duplicated = []
    for identifier in ['device_a', 'device_b']:
        duplicated.extend(dict(row, episode_id=identifier, label='UNLABELED') for row in rows)
    (root / 'policy_history.jsonl').write_text('\n'.join(json.dumps(row) for row in duplicated))
    result = subprocess.run([binary, directory, 'template_evaluate_frozen', '{}'],
                            check=True, capture_output=True, text=True)
    unscored = json.loads(result.stdout)['frozen']
    assert unscored['n_episodes'] == 2 and unscored['unscored'] == 2
    assert unscored['n_false_push'] == 0 and unscored['n_confirmed_leave'] == 0
    assert unscored['missed_leave'] == 0 and unscored['mean_lead_s'] == -1
    print('PASS: simultaneous device isolation and unknown labels excluded from scoring')
