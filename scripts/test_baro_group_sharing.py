"""Synthetic grouping regression; no personal sensor files."""
import tempfile
from pathlib import Path
from validate_raw_frozen_templates import plan_baro_groups

with tempfile.TemporaryDirectory(prefix='commute-baro-groups-') as directory:
    root = Path(directory)
    sessions = []
    for name, rows in [('20260101_100000', 60), ('20260101_100001', 0),
                       ('20260101_100002', 0), ('20260101_101000', 0)]:
        path = root / name
        path.mkdir()
        (path / 'baro_data_test.csv').write_text('wallTsMs,pressure\n' +
            ''.join(f'{1000000+i},1000\n' for i in range(rows)))
        sessions.append(path)
    independent = plan_baro_groups(sessions)
    assert not any(v['baro_shared'] for v in independent.values())
    shared = plan_baro_groups(sessions, True)
    assert shared[sessions[1]]['baro_source'] == sessions[0].name
    assert shared[sessions[2]]['baro_source'] == sessions[0].name
    assert not shared[sessions[0]]['baro_shared']
    assert not shared[sessions[3]]['baro_shared']
    assert shared[sessions[3]]['group_id'] != shared[sessions[0]]['group_id']
    # Chaining close timestamps must not create one group spanning >60 seconds.
    extra = root / '20260101_100059'
    extra.mkdir()
    ambiguous = plan_baro_groups(sessions[:3] + [extra], True)
    assert not any(v['baro_shared'] for v in ambiguous.values())
    print('PASS: opt-in triplet sharing, own priority, no cross-group borrowing, ambiguous group refusal')
