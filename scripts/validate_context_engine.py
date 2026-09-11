"""Isolated real-history comparison; raw replay artifacts stay in local output."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--old-binary', type=Path, required=True)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    def call(binary, root, command, params):
        r = subprocess.run([str(binary.resolve()), str(root.resolve()), command, json.dumps(params)],
                           check=True, text=True, capture_output=True)
        return json.loads(r.stdout)
    def save(name, obj):
        (args.output / name).write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding='utf-8')
    def copy(name):
        dest = args.output / name
        shutil.copytree(args.source, dest, ignore=shutil.ignore_patterns('agent.log', 'agent_trace.jsonl'))
        return dest
    legacy = copy('compatibility')
    params = {'include_prefix_trace': False}
    old = call(args.old_binary, legacy, 'template_evaluate_frozen', params)
    new = call(args.binary, legacy, 'template_evaluate_frozen', params)
    assert old['ok'] and new['ok']
    assert old['frozen']['episode_results'] == new['frozen']['episode_results'], 'legacy behavior changed'
    save('legacy.json', old)
    results = {'legacy': old['frozen']}
    active = json.loads((args.source / 'active_context_template.json').read_text())
    for mode in ('independent_negative', 'conditional_absence'):
        root = copy(mode)
        proposal = {k: active[k] for k in ('anchor_id', 'applicability', 'positive_sequence', 'cancel_paths')}
        proposal.update(template_name='context_engine_' + mode,
                        rationale='Developer-defined controlled validation; not an Agent-discovered structure.')
        if mode == 'independent_negative':
            proposal['negative_pattern'] = 'no_baro_descent'
        else:
            proposal.update(absence_trigger='walking,wifi_detach', absence_expected='baro_descending')
        fitted = call(args.binary, root, 'template_fit', proposal)
        save(mode + '_fit.json', fitted)
        assert fitted['trial']['ok'], fitted
        replay = call(args.binary, root, 'template_evaluate_frozen', {'include_prefix_trace': True})
        save(mode + '_replay.json', replay)
        results[mode] = replay['frozen']
        best = max(fitted['trial']['candidates'], key=lambda c: c['metrics']['score'])
        diagnostic = copy(mode + '_uncommitted_diagnostic')
        # Explicit test fixture only. Rejected candidates never replace source.
        (diagnostic / 'active_context_template.json').write_text(json.dumps(best['template']), encoding='utf-8')
        diagnostic_replay = call(args.binary, diagnostic, 'template_evaluate_frozen', {'include_prefix_trace': True})
        save(mode + '_uncommitted_diagnostic.json', {'eligible': best['eligible'],
             'rejection': best['rejection'], 'candidate': best['template'], 'replay': diagnostic_replay})
        results[mode + '_uncommitted'] = diagnostic_replay['frozen']
        print(mode, 'commit=', fitted['commit'], flush=True)
    summary = {name: {k: r.get(k) for k in ('n_episodes', 'false_kept', 'missed_leave',
        'confirmed_kept', 'mean_lead_s', 'aborted_visible_push', 'score')} for name, r in results.items()}
    save('summary.json', summary)
    print(json.dumps(summary, indent=2), flush=True)

if __name__ == '__main__':
    main()
