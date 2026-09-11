"""Read-only integration check against an existing local replay dataset."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--anchor-id', required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()

def call(command, params):
    run = subprocess.run([str(args.binary.resolve()), str(args.source.resolve()), command,
                          json.dumps(params)], check=True, capture_output=True, text=True)
    return json.loads(run.stdout)

catalog = call('template_catalog', {})  # Initialize ProductStore before the snapshot.
assert 'CURRENT tick' in catalog['event_semantics']['scope']
assert '0.25' in catalog['event_semantics']['no_baro_descent']

def snapshot():
    return {str(f.relative_to(args.source)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in args.source.rglob('*') if f.is_file()}

before = snapshot()
patterns = 'no_baro_descent|no_geo_outbound|no_baro_descent,no_geo_outbound'
result = call('template_batch_negative', {'anchor_id': args.anchor_id, 'candidates': patterns})
assert result['ok'] and result['read_only'] and len(result['candidates']) == 3, result
reference = result['reference']['episode_results']
for candidate in result['candidates']:
    assert len(candidate['replay']['episode_results']) == len(reference)
    assert 'eligible' in candidate and 'rejection' in candidate
for invalid in ['invented_event', 'no_baro_descent|no_baro_descent', 'walking,walking',
                '|walking', 'walking|']:
    assert not call('template_batch_negative', {'anchor_id': args.anchor_id, 'candidates': invalid})['ok']
assert not call('template_batch_negative', {'anchor_id': 'nonexistent', 'candidates': 'walking'})['ok']
assert before == snapshot(), 'read-only batch mutated product files'
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print('PASS: catalog semantics, three real replay candidates, invalid input, product files unchanged')
for c in result['candidates']:
    r = c['replay']
    print(c['negative_pattern'], 'eligible=', c['eligible'], 'reason=', c['rejection'],
          'false_kept=', r.get('false_kept'), 'missed=', r.get('missed_leave'),
          'lead_s=', r.get('mean_lead_s'))
