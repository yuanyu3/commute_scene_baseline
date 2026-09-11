"""Local replay mirror of the product HSMM + service gates."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Any, Dict, Optional

from .anchors import Anchor, AnchorSet
from .geo import Relation, relation_to_anchor
from .hsmm import LeaveHsmm, LeaveObservation, LeavePhase

class Scene(str, Enum):
    AT_HOME = "AT_HOME"
    LEAVING_HOME = "LEAVING_HOME"
    AT_COMPANY = "AT_COMPANY"
    LEAVING_COMPANY = "LEAVING_COMPANY"
    COMMUTE = "COMMUTE"
    AWAY = "AWAY"
    UNKNOWN = "UNKNOWN"


DEFAULT_THETA: Dict[str, Any] = {
    "evidence_strength_version": 1,
    "enter_leave": 0.58,
    # Predictive service gate: PRE_LEAVE plus independent evidence.
    "enter_preleave": 0.50,
    "preleave_min_evidence": 2,
    "preleave_require_walking": True,
    "preleave_require_radio": True,
    "preleave_require_wifi": True,
    "preleave_min_geo": 0.30,
    "preleave_pdr_min_m": 4.0,
    "preleave_pdr_max_m": 8.0,
    "preleave_geo_memory_s": 30.0,
    "exit_leave": 0.45,
    "min_evidence": 2,
    # Direct [0,1] HSMM evidence strengths. Names are retained internally
    # during the compatibility window; no hidden 0.25 + 3*w mapping remains.
    "w_walk": 1.00,
    "w_pdr": 0.85,
    "w_geo": 0.85,
    "w_wifi": 0.61,
    "w_cell": 0.49,
    "w_ble": 0.0,
    "w_time": 0.85,
    # Per-channel hit thresholds (score in [0,1] counts as evidence if >= thr).
    "thr_walk": 0.5,
    "thr_pdr": 0.5,
    "thr_geo": 0.5,
    "thr_wifi": 0.5,
    "thr_cell": 0.5,
    "thr_ble": 0.5,
    "thr_time": 0.5,
    # WiFi continuous score: Jaccard at/below this → s_wifi=1.
    "thr_wifi_jaccard": 0.30,
    # After approach clears, ignore radio leave scores for this many seconds.
    "radio_suppress_after_approach_s": 120.0,
    "weekday_leave_home_hour": 8.25,
    "weekday_leave_company_hour": 18.2,
    "leave_window_min": 25,
    "arm_delay_s": 25,
    "hsmm_preleave_min_s": 10.0,
    "hsmm_preleave_mean_s": 90.0,
    "hsmm_preleave_max_s": 300.0,
    "hsmm_leaving_min_s": 10.0,
    "hsmm_leaving_mean_s": 120.0,
    "hsmm_leaving_max_s": 600.0,
    "hsmm_max_gap_s": 300.0,
    "walk_hold_s": 120.0,
    "lead_min_s": 90,
    "lead_max_s": 240,
    "away_confirm_s": 180,
    "min_away_s": 1200,
    "push_cooldown_s": 1800,
    "max_gps_acc_m": 80.0,
    "allow_network_dwell_acc_m": 120.0,
    # GPS reliability is based on measurable quality, never source_type.
    "gps_low_quality_start_m": 20.0,
    "gps_low_quality_zero_m": 120.0,
    "gps_jump_speed_start_mps": 3.0,
    "gps_jump_speed_zero_mps": 15.0,
    "gps_approach_min_reliability": 0.50,
    "focus_side": "all",
    # Relative vertical-distance gate.  This is physical height, not a
    # building-specific floor count, so it transfers across buildings.
    "baro_gate_enabled": True,
    "baro_min_descent_m": 12.0,
    "w_baro": 0.85,
}


def normalize_evidence_strength(theta: Dict[str, Any]) -> Dict[str, Any]:
    """Normalize new nested strengths or migrate one legacy w_* configuration."""
    out = dict(theta)
    nested = out.get("evidence_strength")
    key_map = {
        "walking": "w_walk",
        "pdr": "w_pdr",
        "geo": "w_geo",
        "wifi": "w_wifi",
        "cell": "w_cell",
        "ble": "w_ble",
        "time": "w_time",
        "baro": "w_baro",
    }
    if isinstance(nested, dict):
        for external, internal in key_map.items():
            if external in nested and nested[external] is not None:
                out[internal] = max(0.0, min(1.0, float(nested[external])))
        out["evidence_strength_version"] = 1
    elif int(out.get("evidence_strength_version", 0)) < 1:
        has_split_radio = any(key in out for key in ("w_wifi", "w_cell", "w_ble"))
        if not has_split_radio and "w_radio" in out:
            radio = float(out["w_radio"])
            out["w_wifi"] = radio * 0.55
            out["w_cell"] = radio * 0.30
            out["w_ble"] = radio * 0.15
        for internal in key_map.values():
            if internal in out:
                legacy = float(out[internal])
                out[internal] = 0.0 if legacy <= 0.0 else min(1.0, 0.25 + 3.0 * legacy)
        out["evidence_strength_version"] = 1
    return out


@dataclass
class TickFeatures:
    t: datetime
    lat: Optional[float] = None
    lon: Optional[float] = None
    acc: Optional[float] = None
    gps_source_type: int = 0
    gps_trust: float = 1.0
    walking: bool = False
    walk_started_at: Optional[datetime] = None
    pdr_net_out_home_m: float = 0.0
    pdr_net_out_company_m: float = 0.0
    wifi_home_detach: bool = False
    wifi_company_detach: bool = False
    wifi_home_attach: bool = False
    wifi_company_attach: bool = False
    cell_leave_home: bool = False
    cell_leave_company: bool = False
    ble_home_detach: bool = False
    ble_company_detach: bool = False
    # Continuous radio features for scoring (1.0 = identical to soft/baseline).
    wifi_jaccard_home: float = 1.0
    wifi_jaccard_company: float = 1.0
    baro_available: bool = False
    # Positive means lower than the origin platform.
    baro_descent_m: float = 0.0
    # Origin platform was established from a short stable baro window while
    # company-workplace radio evidence was present.
    baro_baseline_ready: bool = False
    baro_stable_platform: bool = False
    baro_descending: float = 0.0
    baro_ascending: float = 0.0
    baro_lower_platform: bool = False
    vertical_closure: bool = False
    baro_mode: str = "OFF"


@dataclass
class TickDecision:
    scene: Scene
    score_home: float
    score_company: float
    home_relation: Relation
    company_relation: Relation
    dist_home_m: Optional[float]
    dist_company_m: Optional[float]
    should_service: bool
    service_intent: str
    hsmm_phase_home: str = "AT_ANCHOR"
    hsmm_phase_company: str = "AT_ANCHOR"
    hsmm_preleave_home: float = 0.0
    hsmm_preleave_company: float = 0.0
    hsmm_outside_home: float = 0.0
    hsmm_outside_company: float = 0.0
    evidence: Dict[str, Any] = field(default_factory=dict)
    uncertainty: str = "MEDIUM"
    eta_leave_s: float = -1.0
    lead_gate_ok: bool = False
    push_block_reason: str = "NONE"


def _focus_allows_home(focus: str) -> bool:
    f = (focus or "").lower()
    return f in ("", "both", "home", "all")


def _focus_allows_company(focus: str) -> bool:
    f = (focus or "").lower()
    return f in ("", "both", "company", "all")


def _clip01(x: float) -> float:
    return max(0.0, min(1.0, x))


def _time_prior(t: datetime, center_hour: float, window_min: float) -> float:
    h = t.hour + t.minute / 60.0 + t.second / 3600.0
    half = window_min / 60.0
    d = abs(h - center_hour)
    d = min(d, 24 - d)
    if d <= half:
        return _clip01(1.0 - d / max(half, 1e-3))
    return 0.0


def _wifi_cell_ble_weights(theta: Dict[str, Any]) -> tuple[float, float, float]:
    if "w_wifi" in theta or "w_cell" in theta or "w_ble" in theta:
        return (
            float(theta.get("w_wifi", 0.12)),
            float(theta.get("w_cell", 0.08)),
            float(theta.get("w_ble", 0.0)),
        )
    wr = float(theta.get("w_radio", 0.15))
    return wr * 0.55, wr * 0.30, wr * 0.15


def _wifi_leave_score(jaccard: float, attach: bool, theta: Dict[str, Any]) -> float:
    """Continuous leave score across the detach/attach hysteresis band."""
    if attach:
        return 0.0
    thr = float(theta.get("thr_wifi_jaccard", 0.30))
    thr = max(1e-3, min(0.99, thr))
    if jaccard <= thr:
        return 1.0
    attach_thr = min(1.0, thr + 0.25)
    return _clip01((attach_thr - jaccard) / max(1e-3, attach_thr - thr))


def score_leaving_anchor(
    feat: TickFeatures,
    rel: Relation,
    dist_m: Optional[float],
    r_in: float,
    r_out: float,
    pdr_net_out: float,
    wifi_detach: bool,
    cell_leave: bool,
    ble_detach: bool,
    wifi_jaccard: float,
    wifi_attach: bool,
    center_hour: float,
    theta: Dict[str, Any],
    prev_dist: Optional[float],
    approaching: bool = False,
    radio_suppressed: bool = False,
    gps_reliability: float = 1.0,
) -> tuple[float, Dict[str, Any], int]:
    """Per-channel leave scores → weighted sum; hits use per-channel thresholds."""
    ev: Dict[str, Any] = {}
    hits = 0
    thr_walk = float(theta.get("thr_walk", 0.5))
    thr_pdr = float(theta.get("thr_pdr", 0.5))
    thr_geo = float(theta.get("thr_geo", 0.5))
    thr_wifi = float(theta.get("thr_wifi", 0.5))
    thr_cell = float(theta.get("thr_cell", 0.5))
    thr_ble = float(theta.get("thr_ble", 0.5))
    thr_time = float(theta.get("thr_time", 0.5))
    w_wifi, w_cell, w_ble = _wifi_cell_ble_weights(theta)
    use_walk = float(theta.get("w_walk", 0.0)) > 0.0
    use_pdr = float(theta.get("w_pdr", 0.0)) > 0.0
    use_geo = float(theta.get("w_geo", 0.0)) > 0.0
    use_wifi = w_wifi > 0.0
    use_cell = w_cell > 0.0
    use_ble = w_ble > 0.0
    use_time = float(theta.get("w_time", 0.0)) > 0.0

    s_walk = 1.0 if use_walk and feat.walking else 0.0
    if use_walk and s_walk >= thr_walk:
        hits += 1
    ev["s_walk"] = round(s_walk, 3)
    ev["walk"] = feat.walking

    pdr_eff = 0.0 if approaching else pdr_net_out
    s_pdr = _clip01(pdr_eff / max(15.0, r_in * 0.3)) if use_pdr else 0.0
    if use_pdr and s_pdr >= thr_pdr:
        hits += 1
    ev["s_pdr"] = round(s_pdr, 3)
    ev["pdr_net_out"] = round(pdr_eff, 2)

    s_geo = 0.0
    if use_geo:
        if dist_m is not None and rel in (Relation.INSIDE, Relation.NEAR):
            if prev_dist is not None and dist_m > prev_dist + 3:
                s_geo = _clip01(dist_m / max(r_out, 1))
                if rel == Relation.NEAR and dist_m >= r_in * 0.9:
                    s_geo = max(s_geo, 0.85)
            # No static NEAR leave credit.
        elif rel == Relation.OUTSIDE and not approaching:
            s_geo = 0.8
    s_geo *= _clip01(float(gps_reliability))
    if use_geo and s_geo >= thr_geo:
        hits += 1
    ev["s_geo"] = round(s_geo, 3)
    ev["geo"] = round(s_geo, 2)
    ev["relation"] = rel.value
    ev["dist_m"] = None if dist_m is None else round(dist_m, 1)

    # Continuous WiFi; bool detach is fallback when jaccard not fed.
    s_wifi = _wifi_leave_score(wifi_jaccard, wifi_attach or approaching, theta) if use_wifi else 0.0
    if use_wifi and s_wifi < 1e-6 and wifi_detach and not wifi_attach and not approaching and not radio_suppressed:
        s_wifi = 1.0
    if radio_suppressed:
        s_wifi = 0.0
    if use_wifi and s_wifi >= thr_wifi:
        hits += 1
    ev["s_wifi"] = round(s_wifi, 3)
    ev["wifi_jaccard"] = round(wifi_jaccard, 3)
    ev["wifi_detach"] = wifi_detach
    ev["wifi_attach"] = wifi_attach

    s_cell = 0.0 if (not use_cell or radio_suppressed or approaching or wifi_attach) else (1.0 if cell_leave else 0.0)
    if use_cell and s_cell >= thr_cell:
        hits += 1
    ev["s_cell"] = round(s_cell, 3)
    ev["cell_leave"] = cell_leave

    s_ble = 0.0 if (not use_ble or radio_suppressed or approaching) else (1.0 if ble_detach else 0.0)
    if use_ble and s_ble >= thr_ble:
        hits += 1
    ev["s_ble"] = round(s_ble, 3)
    ev["ble_detach"] = ble_detach

    s_time = _time_prior(feat.t, center_hour, float(theta["leave_window_min"])) if use_time else 0.0
    if use_time and s_time >= thr_time:
        hits += 1
    ev["s_time"] = round(s_time, 3)
    ev["time_prior"] = round(s_time, 2)
    ev["radio_suppressed"] = radio_suppressed

    # Soft anti-return; hard approach gate also in SceneEngine.step.
    if gps_reliability >= float(theta.get("gps_approach_min_reliability", 0.50)) and \
            prev_dist is not None and dist_m is not None and dist_m + 8 < prev_dist:
        ev["toward_anchor"] = True
        return 0.0, ev, 0
    if approaching:
        ev["toward_anchor"] = True
        return 0.0, ev, 0

    channels = {
        "walk": (float(theta["w_walk"]), s_walk),
        "pdr": (float(theta["w_pdr"]), s_pdr),
        "geo": (float(theta["w_geo"]), s_geo),
        "wifi": (w_wifi, s_wifi),
        "cell": (w_cell, s_cell),
        "ble": (w_ble, s_ble),
        "time": (float(theta["w_time"]), s_time),
    }
    score = sum(w * s for w, s in channels.values())
    ev["channels"] = {k: {"w": round(w, 3), "s": round(s, 3)} for k, (w, s) in channels.items()}
    return _clip01(score), ev, hits


class SceneEngine:
    def __init__(self, anchors: AnchorSet, theta: Optional[Dict[str, Any]] = None):
        self.anchors = anchors
        self.theta = {**DEFAULT_THETA, **normalize_evidence_strength(theta or {})}
        self.scene = Scene.UNKNOWN
        self.prev_dist_home: Optional[float] = None
        self.prev_dist_company: Optional[float] = None
        self.prev_rel_home: Relation = Relation.UNKNOWN
        self.prev_rel_company: Relation = Relation.UNKNOWN
        self.prev_t: Optional[datetime] = None
        self._leave_home_since: Optional[datetime] = None
        self._leave_company_since: Optional[datetime] = None
        self._last_push_at: Optional[datetime] = None
        self._outside_home_since: Optional[datetime] = None
        self._outside_company_since: Optional[datetime] = None
        self._leave_home_pushed = False
        self._leave_company_pushed = False
        self._approach_home_streak = 0
        self._approach_company_streak = 0
        self._was_approach_home = False
        self._was_approach_company = False
        self._return_from_outside_home = False
        self._return_from_outside_company = False
        self._radio_suppress_home_until: Optional[datetime] = None
        self._radio_suppress_company_until: Optional[datetime] = None
        self._recent_geo_home_until: Optional[datetime] = None
        self._recent_geo_company_until: Optional[datetime] = None
        self._last_walk_stop_at: Optional[datetime] = None
        self._was_walking = False
        self._hsmm_home = LeaveHsmm()
        self._hsmm_company = LeaveHsmm()

    def _rel(self, feat: TickFeatures, anchor: Anchor) -> tuple[Relation, Optional[float]]:
        if feat.lat is None or feat.lon is None:
            return Relation.UNKNOWN, None
        max_acc = float(self.theta["max_gps_acc_m"])
        if feat.acc is not None and feat.acc > max_acc:
            return Relation.UNKNOWN, None
        return relation_to_anchor(
            feat.lat,
            feat.lon,
            anchor.lat,
            anchor.lon,
            anchor.r_in_m,
            anchor.r_out_m,
        )

    def _gps_fix_usable(self, feat: TickFeatures) -> bool:
        if feat.lat is None or feat.lon is None:
            return False
        max_acc = float(self.theta["max_gps_acc_m"])
        if feat.acc is not None and feat.acc > max_acc and feat.acc > float(
            self.theta["allow_network_dwell_acc_m"]
        ):
            return False
        return True

    def _company_rel(self, feat: TickFeatures) -> tuple[Relation, Optional[float], bool]:
        """Company relation uses the same WGS84 fence as every other anchor."""
        if feat.lat is None or feat.lon is None:
            return Relation.UNKNOWN, None, False
        geo_rel, dist = relation_to_anchor(
            feat.lat,
            feat.lon,
            self.anchors.company.lat,
            self.anchors.company.lon,
            self.anchors.company.r_in_m,
            self.anchors.company.r_out_m,
        )
        if not self._gps_fix_usable(feat):
            return Relation.UNKNOWN, dist, False
        return geo_rel, dist, geo_rel in (Relation.INSIDE, Relation.NEAR)

    def _cooldown_ok(self, t: datetime) -> bool:
        if self._last_push_at is None:
            return True
        return (t - self._last_push_at).total_seconds() >= float(self.theta["push_cooldown_s"])

    def _gps_trust(self, feat: TickFeatures) -> float:
        """Confidence for GPS direction, separate from coarse relation use."""
        trust = _clip01(float(feat.gps_trust))
        if feat.acc is not None:
            start = float(self.theta.get("gps_low_quality_start_m", 20.0))
            zero = float(self.theta.get("gps_low_quality_zero_m", 120.0))
            if feat.acc > start:
                trust *= _clip01((zero - feat.acc) / max(1.0, zero - start))
        return max(0.0, min(1.0, trust))

    def _gps_reliability(self, feat: TickFeatures, dist: Optional[float], prev_dist: Optional[float]) -> float:
        trust = self._gps_trust(feat)
        if dist is not None and prev_dist is not None and self.prev_t is not None and feat.t > self.prev_t:
            dt = (feat.t - self.prev_t).total_seconds()
            speed = abs(dist - prev_dist) / max(dt, 0.001)
            start = float(self.theta.get("gps_jump_speed_start_mps", 3.0))
            zero = max(start + 0.1, float(self.theta.get("gps_jump_speed_zero_mps", 15.0)))
            if speed > start:
                trust *= _clip01((zero - speed) / (zero - start))
        return _clip01(trust)

    def _estimate_eta_out_s(
        self,
        dist_m: Optional[float],
        r_out: float,
        walking: bool,
        pdr_net_out: float,
        prev_dist: Optional[float],
        t: datetime,
        gps_trust: float = 1.0,
    ) -> Optional[float]:
        if dist_m is None:
            return None
        if dist_m >= r_out:
            return 0.0
        remain = max(0.0, r_out - dist_m)
        speed = 0.0
        if gps_trust >= 0.50 and prev_dist is not None and self.prev_t is not None:
            dt = (t - self.prev_t).total_seconds()
            if dt >= 0.4:
                v = (dist_m - prev_dist) / dt
                if v > 0.05:
                    speed = v
        if speed < 0.05 and walking and (pdr_net_out >= 8.0 or remain > 0):
            speed = 1.2
        if speed < 0.05:
            return None
        return remain / speed

    def _update_approach(
        self,
        side: str,
        rel: Relation,
        dist: Optional[float],
        prev_rel: Relation,
        prev_dist: Optional[float],
        gps_trust: float = 1.0,
    ) -> bool:
        """Return True if moving toward this anchor (return / approach)."""
        streak_attr = "_approach_home_streak" if side == "home" else "_approach_company_streak"
        if gps_trust < float(self.theta.get("gps_approach_min_reliability", 0.50)):
            setattr(self, streak_attr, 0)
            return False
        streak = getattr(self, streak_attr)
        approaching = False
        if dist is not None and prev_dist is not None and dist + 3.0 < prev_dist:
            streak += 1
        else:
            streak = 0
        setattr(self, streak_attr, streak)

        if prev_rel == Relation.OUTSIDE and rel in (Relation.NEAR, Relation.INSIDE):
            approaching = True
            if side == "home":
                self._return_from_outside_home = True
            else:
                self._return_from_outside_company = True
        if streak >= 1 and rel in (Relation.NEAR, Relation.INSIDE, Relation.OUTSIDE):
            approaching = True
        if prev_dist is not None and dist is not None and dist + 8.0 < prev_dist:
            approaching = True
        return approaching

    def _note_approach_edge(self, side: str, approaching: bool, t: datetime) -> bool:
        """Arm radio-score suppress only after a real fence re-entry (OUTSIDE→in)."""
        was_attr = "_was_approach_home" if side == "home" else "_was_approach_company"
        until_attr = "_radio_suppress_home_until" if side == "home" else "_radio_suppress_company_until"
        ret_attr = "_return_from_outside_home" if side == "home" else "_return_from_outside_company"
        was = getattr(self, was_attr)
        if was and not approaching and getattr(self, ret_attr):
            hold = float(self.theta.get("radio_suppress_after_approach_s", 120.0))
            setattr(self, until_attr, t + timedelta(seconds=hold))
            setattr(self, ret_attr, False)
        setattr(self, was_attr, approaching)
        until = getattr(self, until_attr)
        return until is not None and t < until

    def step(self, feat: TickFeatures) -> TickDecision:
        if self._was_walking and not feat.walking:
            self._last_walk_stop_at = feat.t
        self._was_walking = feat.walking
        wifi_home_attach = float(self.theta.get("w_wifi", 0.0)) > 0.0 and feat.wifi_home_attach
        wifi_company_attach = float(self.theta.get("w_wifi", 0.0)) > 0.0 and feat.wifi_company_attach
        home, company = self.anchors.home, self.anchors.company
        h_rel, d_home = self._rel(feat, home)
        c_rel, d_co, _ = self._company_rel(feat)
        gps_trust_home = self._gps_reliability(feat, d_home, self.prev_dist_home)
        gps_trust_company = self._gps_reliability(feat, d_co, self.prev_dist_company)

        approach_h = self._update_approach(
            "home", h_rel, d_home, self.prev_rel_home, self.prev_dist_home, gps_trust_home
        )
        approach_c = self._update_approach(
            "company", c_rel, d_co, self.prev_rel_company, self.prev_dist_company, gps_trust_company
        )
        if wifi_home_attach:
            approach_h = True
        if wifi_company_attach:
            approach_c = True

        radio_sup_h = self._note_approach_edge("home", approach_h, feat.t)
        radio_sup_c = self._note_approach_edge("company", approach_c, feat.t)

        sh, eh, hh = score_leaving_anchor(
            feat,
            h_rel,
            d_home,
            home.r_in_m,
            home.r_out_m,
            feat.pdr_net_out_home_m,
            feat.wifi_home_detach,
            feat.cell_leave_home,
            feat.ble_home_detach,
            feat.wifi_jaccard_home,
            wifi_home_attach,
            float(self.theta["weekday_leave_home_hour"]),
            self.theta,
            self.prev_dist_home,
            approaching=approach_h,
            radio_suppressed=radio_sup_h,
            gps_reliability=gps_trust_home,
        )
        sc, ec, hc = score_leaving_anchor(
            feat,
            c_rel,
            d_co,
            company.r_in_m,
            company.r_out_m,
            feat.pdr_net_out_company_m,
            feat.wifi_company_detach,
            feat.cell_leave_company,
            feat.ble_company_detach,
            feat.wifi_jaccard_company,
            wifi_company_attach,
            float(self.theta["weekday_leave_company_hour"]),
            self.theta,
            self.prev_dist_company,
            approaching=approach_c,
            radio_suppressed=radio_sup_c,
            gps_reliability=gps_trust_company,
        )
        eh["approaching"] = approach_h
        ec["approaching"] = approach_c
        eh["gps_trust"] = round(gps_trust_home, 3)
        ec["gps_trust"] = round(gps_trust_company, 3)
        ec["gps_source_type"] = feat.gps_source_type
        ec["gps_source_role"] = "POST_HOC_ONLY"

        def hsmm_observation(ev: Dict[str, Any], rel: Relation, approaching: bool, attached: bool) -> LeaveObservation:
            baro_ready = (
                float(self.theta.get("w_baro", 0.0)) > 0.0
                and feat.baro_available
                and feat.baro_baseline_ready
            )
            return LeaveObservation(
                walking=float(ev.get("s_walk", 0.0)),
                pdr_outbound=float(ev.get("s_pdr", 0.0)),
                geo_outbound=float(ev.get("s_geo", 0.0)),
                wifi_detach=float(ev.get("s_wifi", 0.0)),
                cell_detach=float(ev.get("s_cell", 0.0)),
                ble_detach=float(ev.get("s_ble", 0.0)),
                time_prior=float(ev.get("s_time", 0.0)),
                relation_known=rel != Relation.UNKNOWN,
                inside=rel == Relation.INSIDE,
                near=rel == Relation.NEAR,
                outside=rel == Relation.OUTSIDE,
                approaching=approaching,
                attached=attached,
                baro_descending=feat.baro_descending if baro_ready else 0.0,
                baro_lower_platform=1.0 if baro_ready and feat.baro_lower_platform else 0.0,
                baro_ascending=feat.baro_ascending if baro_ready else 0.0,
                vertical_closure=1.0 if baro_ready and feat.vertical_closure else 0.0,
                baro_available=baro_ready,
            )

        obs_h = hsmm_observation(eh, h_rel, approach_h, wifi_home_attach)
        obs_c = hsmm_observation(ec, c_rel, approach_c, wifi_company_attach)
        hsmm_home = self._hsmm_home.step(obs_h, feat.t, self.theta)
        hsmm_company = self._hsmm_company.step(obs_c, feat.t, self.theta)
        eh["obs"] = asdict(obs_h)
        ec["obs"] = asdict(obs_c)
        # Compatibility fields carry posterior P(LEAVING), matching the C++ product engine.
        sh = hsmm_home.leaving_probability
        sc = hsmm_company.leaving_probability
        eh["hsmm_phase"] = hsmm_home.phase.name
        eh["hsmm_probability"] = hsmm_home.probability
        ec["hsmm_phase"] = hsmm_company.phase.name
        ec["hsmm_probability"] = hsmm_company.probability

        eta_home = self._estimate_eta_out_s(
            d_home, home.r_out_m, feat.walking, feat.pdr_net_out_home_m, self.prev_dist_home, feat.t, gps_trust_home
        )
        eta_co = self._estimate_eta_out_s(
            d_co, company.r_out_m, feat.walking, feat.pdr_net_out_company_m, self.prev_dist_company, feat.t,
            gps_trust_company
        )

        need = int(self.theta["min_evidence"])
        should_service = False
        intent = "NONE"
        push_block = "NONE"
        new_scene = self.scene
        active_eta: Optional[float] = None
        allow_home = _focus_allows_home(str(self.theta.get("focus_side", "all")))
        allow_company = _focus_allows_company(str(self.theta.get("focus_side", "all")))

        if h_rel == Relation.OUTSIDE:
            self._outside_home_since = self._outside_home_since or feat.t
        else:
            self._outside_home_since = None
        if c_rel == Relation.OUTSIDE:
            self._outside_company_since = self._outside_company_since or feat.t
        else:
            self._outside_company_since = None

        exit_leave = float(self.theta["exit_leave"])
        if allow_home and h_rel == Relation.INSIDE and sh <= exit_leave:
            new_scene = Scene.AT_HOME
            self._leave_home_since = None
            self._leave_home_pushed = False
        elif allow_company and c_rel == Relation.INSIDE and sc <= exit_leave:
            new_scene = Scene.AT_COMPANY
            self._leave_company_since = None
            self._leave_company_pushed = False

        enter = float(self.theta["enter_leave"])
        home_leave_cand = (
            allow_home
            and h_rel in (Relation.INSIDE, Relation.NEAR)
            and not approach_h
            and self.prev_rel_home != Relation.OUTSIDE
            and sh >= enter
        )
        co_leave_cand = (
            allow_company
            and c_rel in (Relation.INSIDE, Relation.NEAR)
            and not approach_c
            and self.prev_rel_company != Relation.OUTSIDE
            and sc >= enter
        )

        # ETA remains diagnostic. It does not block a valid HSMM departure;
        # lead_min/lead_max are evaluated only by offline scoring.
        lead_ok = True
        if home_leave_cand:
            new_scene = Scene.LEAVING_HOME
            self._leave_home_since = self._leave_home_since or feat.t
            active_eta = eta_home
            if wifi_home_attach or approach_h:
                push_block = "APPROACHING"
            elif h_rel == Relation.OUTSIDE:
                push_block = "OUTSIDE"
            elif self._leave_home_pushed:
                push_block = "ALREADY_PUSHED"
            elif not self._cooldown_ok(feat.t):
                push_block = "COOLDOWN"
            else:
                should_service = True
                intent = "DEPARTURE_NOTIFICATION"
                self._leave_home_pushed = True
                self._last_push_at = feat.t
        elif co_leave_cand:
            new_scene = Scene.LEAVING_COMPANY
            self._leave_company_since = self._leave_company_since or feat.t
            active_eta = eta_co
            if wifi_company_attach or approach_c:
                push_block = "APPROACHING"
            elif c_rel == Relation.OUTSIDE:
                push_block = "OUTSIDE"
            elif self._leave_company_pushed:
                push_block = "ALREADY_PUSHED"
            elif not self._cooldown_ok(feat.t):
                push_block = "COOLDOWN"
            else:
                should_service = True
                intent = "LEAVE_COMPANY_NOTIFICATION"
                self._leave_company_pushed = True
                self._last_push_at = feat.t

        def _persist(since: Optional[datetime]) -> bool:
            if since is None:
                return False
            return (feat.t - since).total_seconds() >= float(self.theta["away_confirm_s"])

        leaving_now = new_scene in (Scene.LEAVING_HOME, Scene.LEAVING_COMPANY)
        if not leaving_now and _persist(self._outside_home_since) and _persist(self._outside_company_since):
            new_scene = Scene.COMMUTE
        elif not leaving_now and _persist(self._outside_home_since) and c_rel == Relation.INSIDE:
            new_scene = Scene.AT_COMPANY
        elif not leaving_now and _persist(self._outside_company_since) and h_rel == Relation.INSIDE:
            new_scene = Scene.AT_HOME
        elif _persist(self._outside_home_since) and new_scene == Scene.LEAVING_HOME:
            if self._leave_home_since and (feat.t - self._leave_home_since).total_seconds() >= float(
                self.theta["away_confirm_s"]
            ):
                new_scene = Scene.AT_COMPANY if c_rel == Relation.INSIDE else Scene.COMMUTE
        elif _persist(self._outside_company_since) and new_scene == Scene.LEAVING_COMPANY:
            if self._leave_company_since and (feat.t - self._leave_company_since).total_seconds() >= float(
                self.theta["away_confirm_s"]
            ):
                new_scene = Scene.AT_HOME if h_rel == Relation.INSIDE else Scene.COMMUTE

        if (
            should_service
            and intent == "DEPARTURE_NOTIFICATION"
            and (h_rel == Relation.OUTSIDE or approach_h or wifi_home_attach)
        ):
            should_service = False
            intent = "NONE"
            push_block = "APPROACHING" if approach_h or wifi_home_attach else "OUTSIDE"
            self._leave_home_pushed = False
        if (
            should_service
            and intent == "LEAVE_COMPANY_NOTIFICATION"
            and (c_rel == Relation.OUTSIDE or approach_c or wifi_company_attach)
        ):
            should_service = False
            intent = "NONE"
            push_block = "APPROACHING" if approach_c or wifi_company_attach else "OUTSIDE"
            self._leave_company_pushed = False

        uncertainty = "MEDIUM"
        max_hits = max(hh, hc)
        if max_hits >= 3:
            uncertainty = "LOW"
        elif max_hits < need:
            uncertainty = "HIGH"

        self.scene = new_scene
        self.prev_rel_home = h_rel
        self.prev_rel_company = c_rel
        if d_home is not None:
            self.prev_dist_home = d_home
        if d_co is not None:
            self.prev_dist_company = d_co
        self.prev_t = feat.t

        return TickDecision(
            scene=new_scene,
            score_home=sh,
            score_company=sc,
            home_relation=h_rel,
            company_relation=c_rel,
            dist_home_m=d_home,
            dist_company_m=d_co,
            should_service=should_service,
            service_intent=intent,
            hsmm_phase_home=hsmm_home.phase.name,
            hsmm_phase_company=hsmm_company.phase.name,
            hsmm_preleave_home=hsmm_home.probability[LeavePhase.PRE_LEAVE],
            hsmm_preleave_company=hsmm_company.probability[LeavePhase.PRE_LEAVE],
            hsmm_outside_home=hsmm_home.probability[LeavePhase.OUTSIDE],
            hsmm_outside_company=hsmm_company.probability[LeavePhase.OUTSIDE],
            evidence={"home": eh, "company": ec, "hits_home": hh, "hits_company": hc},
            uncertainty=uncertainty,
            eta_leave_s=-1.0 if active_eta is None else float(active_eta),
            lead_gate_ok=lead_ok if leaving_now else False,
            push_block_reason=push_block,
        )
