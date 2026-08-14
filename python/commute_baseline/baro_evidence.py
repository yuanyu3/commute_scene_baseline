"""Deterministic relative-height evidence from a barometer stream."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime
from typing import Deque, Optional, Tuple


@dataclass
class BaroSnapshot:
    available: bool = False
    baseline_ready: bool = False
    pressure_hpa: Optional[float] = None
    descent_m: float = 0.0
    descending: float = 0.0
    lower_platform: bool = False
    stable_platform: bool = False


class BaroEvidence:
    def __init__(self, stable_window_s: float = 6.0, stable_span_hpa: float = 0.12):
        self.stable_window_s = stable_window_s
        self.stable_span_hpa = stable_span_hpa
        self.samples: Deque[Tuple[datetime, float]] = deque()
        self.baseline_hpa: Optional[float] = None
        self.prev_pressure: Optional[float] = None

    def observe(self, t: datetime, pressure_hpa: float) -> None:
        if not 850.0 <= pressure_hpa <= 1100.0:
            return
        self.samples.append((t, pressure_hpa))
        while self.samples and (t - self.samples[0][0]).total_seconds() > 20.0:
            self.samples.popleft()

    @staticmethod
    def _descent_m(baseline_hpa: float, pressure_hpa: float) -> float:
        return max(0.0, 44330.0 * ((pressure_hpa / baseline_hpa) ** 0.1903 - 1.0))

    def evaluate(self, t: datetime, workplace_ready: bool, min_descent_m: float = 12.0) -> BaroSnapshot:
        recent = [(ts, p) for ts, p in self.samples if (t - ts).total_seconds() <= self.stable_window_s]
        if not recent or (t - recent[-1][0]).total_seconds() > 5.0:
            return BaroSnapshot()
        pressure = sum(p for _, p in recent[-20:]) / min(20, len(recent))
        stable = len(recent) >= 5 and max(p for _, p in recent) - min(p for _, p in recent) <= self.stable_span_hpa
        if self.baseline_hpa is None and workplace_ready and stable:
            self.baseline_hpa = pressure
        descent = 0.0 if self.baseline_hpa is None else self._descent_m(self.baseline_hpa, pressure)
        descending = 0.0
        if self.prev_pressure is not None:
            descending = max(0.0, min(1.0, (pressure - self.prev_pressure) / 0.18))
        self.prev_pressure = pressure
        return BaroSnapshot(
            available=True,
            baseline_ready=self.baseline_hpa is not None,
            pressure_hpa=pressure,
            descent_m=descent,
            descending=descending,
            stable_platform=stable,
            lower_platform=stable and descent >= min_descent_m,
        )
