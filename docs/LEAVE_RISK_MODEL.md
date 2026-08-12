# Lightweight Leave Risk Model

This offline prototype predicts whether outdoor confirmation (`GPS source_type=1`) will occur within 30, 60, or
120 seconds. The confirmation is used only as a delayed training label and is never an online feature.

## Data split

`config/0811_event_groups.json` groups parallel device captures from the same physical behavior. Training and
validation split by `group_id`, so replicas of one behavior cannot leak across folds. `UNKNOWN` groups are excluded.

The feature library is scene-neutral: multi-scale inertial statistics, walking duration, PDR trajectory statistics,
WiFi core coverage trends, Cell set membership/change counts, and missing-channel masks. Route names such as elevator
or stairs are evaluation metadata, not model inputs.

## Agent-style search

`examples/train_leave_risk.py` evaluates feature-group candidates, risk thresholds, and persistence lengths. Every
candidate receives:

1. leave-one-event-group-out validation;
2. positive-route-family holdout;
3. sensor-group ablation;
4. penalties for false-push groups, missed leave groups, and disagreement among parallel captures.

The selected v1 candidate uses normalized inertial trends plus walking duration, with `risk_120 >= 0.60` for three
consecutive 5-second ticks. It does not depend on WiFi, Cell, or PDR. This is a result on the current small dataset,
not a permanent claim that those channels are useless.

Risk probabilities enter `LeaveHsmm` as observation likelihoods. The model fuses features; the HSMM supplies temporal
continuity and suppresses one-tick spikes.

## Reproduce

```powershell
python -m pip install -r requirements-risk.txt
python examples\train_leave_risk.py --out output\leave_risk_v5
python examples\replay_leave_risk.py `
  --model config\leave_risk_model_v1.json `
  --out output\leave_risk_v5\replay
```

Strict group-held-out evaluation currently detects all 10 leave behavior groups and produces no push in either known
fifth-floor indoor-activity group. It detects 25/26 leave replicas, with one parallel-capture disagreement and a
median lead of about 49 seconds. Complete route-family holdout recall is only 60% in the worst route family, so the
model is an offline prototype and must not replace the phone production gate yet.

The full-data fit replays all 26 leave captures and all six indoor captures correctly, but that number is training-set
replay and must not be reported as unseen-data generalization.
