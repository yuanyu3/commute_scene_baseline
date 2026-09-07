"""One bounded Jiuwen diagnostic run in an isolated copy; never mutates a live profile."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--source', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--env-file', type=Path, required=True)
ap.add_argument('--binary', type=Path, required=True)
args = ap.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
for name in ['policy_history.jsonl', 'theta.json', 'active_context_template.json']:
    shutil.copy2(args.source / name, args.output / name)
shutil.copy2(ROOT / 'config/anchors.json', args.output / 'anchors.json')
# This diagnostic uses policy_history directly; do not fabricate push labels.
if (args.source / 'leave_episodes.jsonl').is_file():
    shutil.copy2(args.source / 'leave_episodes.jsonl', args.output / 'leave_episodes.jsonl')
else:
    (args.output / 'leave_episodes.jsonl').write_text('')
query = args.output / 'query.txt'
query.write_text('本次只诊断现有模板，不生成或提交新模板。请先查询活动模板，'
    '调用diagnose_context_template分别关闭正向与负向证据，检验它们对推送的实际作用。'
    '逐episode检查就绪、完成、概率越线、门控与推送，区分匹配和实际收益。'
    '区分CONFIRMED_LEAVE、软正和UNLABELED，不把未标注视为正确或误推。'
    '输出支持、反证、缺失证据和保留/质疑建议，记录审计即可；无需索取用户反馈。')
before = {name: (args.output / name).read_bytes() for name in ['theta.json', 'active_context_template.json']}
with (args.output / 'agent.log').open('w') as log:
    try:
        result = subprocess.run([str(args.binary.resolve()), str(args.env_file.resolve()),
            str(args.output.resolve()), '--no-fixture', '--diagnostic-only', '--max-turn', '12',
            '--query-file', str(query.resolve())], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=240)
        status = {'returncode': result.returncode, 'timed_out': False}
    except subprocess.TimeoutExpired:
        status = {'returncode': None, 'timed_out': True}
status['profile_unchanged'] = all((args.output / name).read_bytes() == value for name, value in before.items())
(args.output / 'status.json').write_text(json.dumps(status))
print(json.dumps(status))
raise SystemExit(3 if not status['profile_unchanged'] else 124 if status['timed_out'] else status['returncode'])
