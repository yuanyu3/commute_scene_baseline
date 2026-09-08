"""Run bounded Agent return-path synthesis in an isolated product root (WSL/Linux).

Keeps raw-label evaluation separate from Agent interpretations. Credentials are
passed as a file path to the host, never loaded or printed by this script.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--env-file', type=Path, required=True)
    parser.add_argument('--model', default='')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for name in ['theta.json', 'anchors.json', 'policy_history.jsonl',
                 'leave_episodes.jsonl', 'active_context_template.json']:
        shutil.copy2(args.source / name, out / name)
    query = out / 'query.txt'
    query.write_text(
        '根据本目录完整多日历史，研究可在线识别的返回路径，生成受约束模板。'
        '先查询目录和候选证据，对每个返回候选给出处理状态与依据，比较确认离开和未返回样本。'
        '按需查询时间窗口、执行序列消融；证据不足则保留未知，记录缺失信息。'
        '仅在工具验证通过且逐episode推送不退化时提交模板；不得直接改数值参数。'
        '实际通知结果与过程解释分开报告。无需向用户提问。', encoding='utf-8')
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(root / 'third_party/bbpjiuwen/lib') + ':' + env.get('LD_LIBRARY_PATH', '')
    if args.model:
        env['JIUWEN_MODEL'] = args.model
    theta_before = (out / 'theta.json').read_bytes()
    with (out / 'agent.log').open('w') as log:
        try:
            run = subprocess.run([str(root / 'examples/personalizer_llm/build/personalizer_llm'),
                str(args.env_file.resolve()), str(out), '--no-fixture', '--max-turn', '28',
                '--system-prompt', str(root / 'jiuwen_agent/system_prompt.md'),
                '--query-file', str(query)], cwd=root, env=env, stdout=log,
                stderr=subprocess.STDOUT, timeout=args.timeout)
            status = {'returncode': run.returncode, 'timed_out': False}
        except subprocess.TimeoutExpired:
            status = {'returncode': None, 'timed_out': True}
    status['theta_unchanged'] = theta_before == (out / 'theta.json').read_bytes()
    (out / 'run_status.json').write_text(json.dumps(status, indent=2))
    print(json.dumps(status), flush=True)
    return 124 if status['timed_out'] else status['returncode']


if __name__ == '__main__':
    raise SystemExit(main())
