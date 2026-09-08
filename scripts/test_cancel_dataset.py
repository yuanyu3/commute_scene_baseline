"""Regression tests for evidence multiplicity and annotation provenance."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_cancel_sequence_multiday as prepare


class CancelDatasetTest(unittest.TestCase):
    def test_each_source_once_and_no_fabricated_notification(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            history = root / 'output/day/frozen'
            history.mkdir(parents=True)
            rows = [dict(episode_id=e, side='company', t_ms=t, outcome_t_ms=t + 10)
                    for e, t in [('a', 100), ('b', 200)]]
            (history / 'policy_history.jsonl').write_text('\n'.join(map(json.dumps, rows)))
            (root / 'config').mkdir()
            for name in ['theta_default.json', 'anchors.json']:
                (root / 'config' / name).write_text('{}')
            episodes = [('day', 'a', 'FALSE_PUSH'), ('day', 'b', 'CONFIRMED_LEAVE')]
            dest = root / 'result'
            with patch.object(prepare, 'ROOT', root), patch.object(prepare, 'EPISODES', episodes), \
                    patch('sys.argv', ['prepare', '--output', str(dest)]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(prepare.main(), 0)
            self.assertEqual(len((dest / 'policy_history.jsonl').read_text().splitlines()), 2)
            for line in (dest / 'leave_episodes.jsonl').read_text().splitlines():
                self.assertNotIn('t_push_ms', json.loads(line))
            with (history / 'policy_history.jsonl').open('a') as stream:
                stream.write('\n' + json.dumps(rows[0]))
            with patch.object(prepare, 'ROOT', root), patch.object(prepare, 'EPISODES', episodes), \
                    patch('sys.argv', ['prepare', '--output', str(root / 'duplicate')]):
                with self.assertRaisesRegex(SystemExit, 'duplicate'):
                    prepare.main()


if __name__ == '__main__':
    unittest.main()
