"""Online explicit-duration leave HSMM used by the local replay engine."""

from __future__ import annotations

import math
from dataclasses import dataclass, replace
from datetime import datetime
from enum import IntEnum
from typing import Any, Dict, List


class LeavePhase(IntEnum):
    AT_ANCHOR = 0
    PRE_LEAVE = 1
    LEAVING = 2
    OUTSIDE = 3


@dataclass
class LeaveObservation:
    walking: float = 0.0
    pdr_outbound: float = 0.0
    geo_outbound: float = 0.0
    wifi_detach: float = 0.0
    cell_detach: float = 0.0
    ble_detach: float = 0.0
    time_prior: float = 0.0
    relation_known: bool = False
    inside: bool = False
    near: bool = False
    outside: bool = False
    approaching: bool = False
    attached: bool = False
    risk_available: bool = False
    risk_30s: float = 0.0
    risk_60s: float = 0.0
    risk_120s: float = 0.0
    baro_descending: float = 0.0
    baro_lower_platform: float = 0.0
    baro_available: bool = False
    sequence_available: bool = False
    sequence_progress: float = 0.0
    sequence_complete: float = 0.0
    sequence_ready: float = -1.0
    negative_pattern_match: float = 0.0
    sequence_reliability: float = 0.0


@dataclass
class LeaveHsmmResult:
    phase: LeavePhase
    probability: List[float]

    @property
    def leaving_probability(self) -> float:
        return self.probability[LeavePhase.LEAVING]


def _clip01(value: float) -> float:
    return max(0.0, min(1.0, value))


class LeaveHsmm:
    """Sparse duration-hypothesis mirror of the C++ product HSMM."""

    _MAX_AGE_S = 3600
    _MAX_HYPOTHESES_PER_STATE = 256

    def __init__(self) -> None:
        self._mass: List[Dict[int, float]] = [{}, {}, {}, {}]
        self._last_t: datetime | None = None

    def reset(self) -> None:
        self._mass = [{}, {}, {}, {}]
        self._last_t = None

    @staticmethod
    def _effective_observation(obs: LeaveObservation, theta: Dict[str, Any]) -> LeaveObservation:
        """Mask disabled channels before both transition and emission calculations."""
        values: Dict[str, Any] = {}
        if float(theta.get("w_walk", 0.0)) <= 0.0:
            values["walking"] = 0.0
        if float(theta.get("w_pdr", 0.0)) <= 0.0:
            values["pdr_outbound"] = 0.0
        if float(theta.get("w_geo", 0.0)) <= 0.0:
            values["geo_outbound"] = 0.0
        if float(theta.get("w_wifi", 0.0)) <= 0.0:
            values.update(wifi_detach=0.0, attached=False)
        if float(theta.get("w_cell", 0.0)) <= 0.0:
            values["cell_detach"] = 0.0
        if float(theta.get("w_ble", 0.0)) <= 0.0:
            values["ble_detach"] = 0.0
        if float(theta.get("w_time", 0.0)) <= 0.0:
            values["time_prior"] = 0.0
        if float(theta.get("w_baro", 0.0)) <= 0.0:
            values.update(baro_descending=0.0, baro_lower_platform=0.0, baro_available=False)
        if float(theta.get("w_risk", 0.8)) <= 0.0:
            values.update(risk_available=False, risk_30s=0.0, risk_60s=0.0, risk_120s=0.0)
        return replace(obs, **values) if values else obs

    def _initialize(self, obs: LeaveObservation, t: datetime) -> LeaveHsmmResult:
        initial = LeavePhase.OUTSIDE if obs.outside else LeavePhase.AT_ANCHOR
        self._mass = [{}, {}, {}, {}]
        self._mass[initial][0] = 1.0
        self._last_t = t
        return self._summarize()

    @staticmethod
    def _exit_probability(
        phase: LeavePhase, age_s: int, dt_s: int, obs: LeaveObservation, theta: Dict[str, Any]
    ) -> float:
        if phase == LeavePhase.AT_ANCHOR:
            # Observation-independent candidate flow. Atomic observations are
            # consumed once, by _emission().
            hazard = max(0.0, float(theta.get("hsmm_at_anchor_exit_hazard_per_s", 0.0030)))
            return _clip01(1.0 - math.exp(-dt_s * hazard))
        if phase == LeavePhase.PRE_LEAVE:
            minimum = float(theta.get("hsmm_preleave_min_s", 10.0))
            if age_s < minimum:
                return 0.0
            if age_s >= float(theta.get("hsmm_preleave_max_s", 300.0)):
                return 1.0
            scale = max(5.0, float(theta.get("hsmm_preleave_mean_s", 90.0)) - minimum)
            return _clip01(1.0 - math.exp(-dt_s / scale))
        if phase == LeavePhase.LEAVING:
            if obs.outside:
                return 0.95
            minimum = float(theta.get("hsmm_leaving_min_s", 10.0))
            if age_s < minimum:
                return 0.0
            if age_s >= float(theta.get("hsmm_leaving_max_s", 600.0)):
                return 1.0
            scale = max(5.0, float(theta.get("hsmm_leaving_mean_s", 120.0)) - minimum)
            return _clip01(1.0 - math.exp(-dt_s / scale))
        return 0.95 if obs.inside else 0.0

    @staticmethod
    def _emission(obs: LeaveObservation, theta: Dict[str, Any]) -> List[float]:
        x = [
            obs.walking,
            obs.pdr_outbound,
            obs.geo_outbound,
            obs.wifi_detach,
            obs.cell_detach,
            obs.ble_detach,
            obs.time_prior,
            obs.baro_descending,
            obs.baro_lower_platform,
        ]
        expected = [
            [0.08, 0.03, 0.03, 0.05, 0.08, 0.08, 0.25, 0.03, 0.02],
            [0.65, 0.24, 0.12, 0.16, 0.12, 0.10, 0.62, 0.30, 0.08],
            [0.92, 0.72, 0.72, 0.62, 0.40, 0.24, 0.72, 0.80, 0.72],
            [0.65, 0.55, 0.96, 0.88, 0.62, 0.30, 0.45, 0.08, 0.82],
        ]
        keys = ["w_walk", "w_pdr", "w_geo", "w_wifi", "w_cell", "w_ble", "w_time", "w_baro", "w_baro"]
        feature_reliability = [
            0.0 if float(theta.get(key, 0.0)) <= 0.0 else 0.25 + 3.0 * float(theta.get(key, 0.0))
            for key in keys
        ]
        logs: List[float] = []
        for state in range(4):
            value = 0.0
            for feature, observation in enumerate(x):
                if feature >= 7 and not obs.baro_available:
                    continue
                mu = max(0.02, min(0.98, expected[state][feature]))
                sample = _clip01(observation)
                value += feature_reliability[feature] * (
                    sample * math.log(mu) + (1.0 - sample) * math.log(1.0 - mu)
                )
            if obs.relation_known:
                if obs.inside:
                    # Structural relation evidence must not inspect atomic
                    # sensors a second time. Predictive LEAVING remains possible.
                    relation = [0.65, 0.65, 0.65, 0.02]
                elif obs.near:
                    relation = [0.18, 0.48, 0.72, 0.12]
                elif obs.outside:
                    relation = [0.01, 0.03, 0.10, 0.97]
                else:
                    relation = [0.25, 0.25, 0.25, 0.25]
                value += 3.0 * math.log(max(1e-9, relation[state]))
            if obs.approaching or obs.attached:
                value += 2.5 * math.log([0.92, 0.30, 0.02, 0.08][state])
            if obs.sequence_available:
                progress_llr = [-0.35, 0.65, 0.25, -0.30]
                complete_llr = [-1.00, 0.15, 1.20, -0.35]
                negative_llr = [0.80, 0.25, -1.00, -0.25]
                seq_reliability = _clip01(obs.sequence_reliability)
                if obs.sequence_ready >= 0.0:
                    value += 2.0 * seq_reliability * _clip01(obs.sequence_ready) * complete_llr[state]
                else:
                    value += 2.0 * seq_reliability * _clip01(obs.sequence_progress) * progress_llr[state]
                    value += 2.0 * seq_reliability * _clip01(obs.sequence_complete) * complete_llr[state]
                value += 2.0 * seq_reliability * _clip01(obs.negative_pattern_match) * negative_llr[state]
            if obs.risk_available:
                risk_expected = [
                    [0.03, 0.05, 0.08],
                    [0.22, 0.48, 0.72],
                    [0.72, 0.84, 0.90],
                    [0.30, 0.35, 0.40],
                ][state]
                risk_weight = float(theta.get("w_risk", 0.8))
                for sample, mu in zip(
                    [obs.risk_30s, obs.risk_60s, obs.risk_120s], risk_expected
                ):
                    sample = _clip01(sample)
                    value += risk_weight * (
                        sample * math.log(mu) + (1.0 - sample) * math.log(1.0 - mu)
                    )
            logs.append(value)
        maximum = max(logs)
        return [max(1e-9, math.exp(value - maximum)) for value in logs]

    @classmethod
    def _prune(cls, state_mass: Dict[int, float]) -> Dict[int, float]:
        kept = [(age, value) for age, value in state_mass.items() if value >= 1e-10]
        if len(kept) > cls._MAX_HYPOTHESES_PER_STATE:
            kept = sorted(kept, key=lambda item: item[1], reverse=True)[: cls._MAX_HYPOTHESES_PER_STATE]
        return dict(kept)

    def step(self, obs: LeaveObservation, t: datetime, theta: Dict[str, Any]) -> LeaveHsmmResult:
        obs = self._effective_observation(obs, theta)
        if self._last_t is None:
            return self._initialize(obs, t)
        raw_dt = (t - self._last_t).total_seconds()
        if raw_dt <= 0 or raw_dt > float(theta.get("hsmm_max_gap_s", 300.0)):
            return self._initialize(obs, t)
        dt_s = max(1, min(60, round(raw_dt)))
        self._last_t = t
        predicted: List[Dict[int, float]] = [{}, {}, {}, {}]

        def add(state: LeavePhase, age: int, value: float) -> None:
            predicted[state][age] = predicted[state].get(age, 0.0) + value

        for state_index, hypotheses in enumerate(self._mass):
            phase = LeavePhase(state_index)
            for age, source in hypotheses.items():
                next_age = min(self._MAX_AGE_S, age + dt_s)
                exit_p = self._exit_probability(phase, next_age, dt_s, obs, theta)
                add(phase, next_age, source * (1.0 - exit_p))
                exiting = source * exit_p
                if phase == LeavePhase.AT_ANCHOR:
                    add(LeavePhase.PRE_LEAVE, 0, exiting)
                elif phase == LeavePhase.PRE_LEAVE:
                    share = _clip01(float(theta.get("hsmm_preleave_exit_to_leaving", 0.90)))
                    add(LeavePhase.LEAVING, 0, exiting * share)
                    add(LeavePhase.AT_ANCHOR, 0, exiting * (1.0 - share))
                elif phase == LeavePhase.LEAVING:
                    share = 0.99 if obs.outside else 0.02
                    add(LeavePhase.OUTSIDE, 0, exiting * share)
                    add(LeavePhase.PRE_LEAVE, 0, exiting * (1.0 - share))
                else:
                    add(LeavePhase.AT_ANCHOR, 0, exiting)

        likelihood = self._emission(obs, theta)
        total = 0.0
        for state in range(4):
            predicted[state] = self._prune(
                {age: value * likelihood[state] for age, value in predicted[state].items()}
            )
            total += sum(predicted[state].values())
        if total <= 1e-9 or not math.isfinite(total):
            return self._initialize(obs, t)
        self._mass = [
            {age: value / total for age, value in state_mass.items()} for state_mass in predicted
        ]
        return self._summarize()

    def _summarize(self) -> LeaveHsmmResult:
        probability = [sum(state.values()) for state in self._mass]
        phase = LeavePhase(max(range(4), key=probability.__getitem__))
        return LeaveHsmmResult(phase=phase, probability=probability)
