"""Compress WiFi/Cell/BLE streams into detach/attach flags for SceneEngine."""

from __future__ import annotations

import csv
import glob
import json
import os
from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Deque, Dict, Iterable, List, Optional, Set, Tuple


@dataclass
class WifiAp:
    bssid: str
    rssi: int = -127


@dataclass
class WifiScan:
    t_ms: int
    aps: List[WifiAp] = field(default_factory=list)


@dataclass
class CellSample:
    t_ms: int
    cell_id: int = 0
    rssi: int = -127


@dataclass
class BleSample:
    t_ms: int
    mac: str = ""
    rssi: int = -127


@dataclass
class RadioConfig:
    ble_evidence_enabled: bool = False
    rssi_min: int = -85
    rssi_strong: int = -75
    dwell_learn_ms: int = 180_000
    dwell_set_size: int = 8
    dwell_min_count: int = 3
    jaccard_detach: float = 0.30
    jaccard_churn: float = 0.35
    churn_baseline_ms: int = 120_000
    rssi_drop_db: float = 12.0
    cell_stable_ms: int = 90_000
    site_cell_window_ms: int = 15_000
    site_cell_detach_match_ratio: float = 0.20
    site_cell_attach_match_ratio: float = 0.60
    site_cell_min_samples: int = 4
    site_cell_confirm_samples: int = 3
    detach_confirm_ms: int = 15_000
    detach_confirm_scans: int = 2
    history_cap: int = 40
    # Attach: strong-AP surge vs baseline (return-to-anchor).
    attach_n_strong_delta: int = 5
    attach_n_strong_abs: int = 8
    jaccard_attach: float = 0.55
    site_wifi_detach_recall: float = 0.20
    site_wifi_attach_recall: float = 0.50
    site_wifi_detach_confirm_scans: int = 3
    site_wifi_attach_confirm_scans: int = 2
    site_wifi_detach_confirm_ms: int = 30_000
    site_wifi_attach_confirm_ms: int = 15_000


@dataclass
class RadioSnapshot:
    wifi_home_detach: bool = False
    wifi_company_detach: bool = False
    wifi_home_attach: bool = False
    wifi_company_attach: bool = False
    cell_leave_home: bool = False
    cell_leave_company: bool = False
    ble_home_detach: bool = False
    ble_company_detach: bool = False
    jaccard_home: float = 1.0
    jaccard_company: float = 1.0
    jaccard_churn: float = 1.0
    home_dwell_ready: bool = False
    company_dwell_ready: bool = False
    n_strong: int = 0
    # Recall of the static workplace-floor fingerprint.
    company_site_wifi_coverage: float = 0.0
    company_site_wifi_matches: int = 0
    company_site_cell_match: bool = False
    company_site_cell_match_ratio: float = 0.0
    company_site_ble_matches: int = 0
    reason: str = ""


@dataclass
class _Dwell:
    inside_since_ms: int = 0
    inside_accum_ms: int = 0
    last_inside_ms: int = 0
    was_inside: bool = False
    bssid_count: Dict[str, int] = field(default_factory=dict)
    bssid_rssi: Dict[str, Deque[int]] = field(default_factory=dict)
    soft_set: Set[str] = field(default_factory=set)
    ready: bool = False


def _jaccard(a: Set[str], b: Set[str]) -> float:
    if not a and not b:
        return 1.0
    if not a or not b:
        return 0.0
    inter = len(a & b)
    uni = len(a | b)
    return inter / uni if uni else 1.0


def _strong_set(scan: WifiScan, rssi_min: int) -> Set[str]:
    return {ap.bssid.lower() for ap in scan.aps if ap.bssid and ap.rssi >= rssi_min}


def _n_strong(scan: WifiScan, rssi_strong: int) -> int:
    return sum(1 for ap in scan.aps if ap.bssid and ap.rssi >= rssi_strong)


class RadioEvidence:
    """Online radio compressor (mirrors sa_cpp RadioEvidence + attach)."""

    def __init__(self, cfg: Optional[RadioConfig] = None):
        self.cfg = cfg or RadioConfig()
        self.wifi_history: Deque[WifiScan] = deque()
        self.cell_history: Deque[CellSample] = deque()
        self.ble_history: Deque[BleSample] = deque()
        self.home = _Dwell()
        self.company = _Dwell()
        self.stable_cell_id = 0
        self.stable_cell_since_ms = 0
        self._home_detach_streak = 0
        self._company_detach_streak = 0
        self._home_detach_since = 0
        self._company_detach_since = 0
        self._home_attach_streak = 0
        self._company_attach_streak = 0
        self._home_attach_since = 0
        self._company_attach_since = 0
        self._cell_home_streak = 0
        self._cell_company_streak = 0
        self._ble_home_streak = 0
        self._ble_company_streak = 0
        self._ble_home_since = 0
        self._ble_company_since = 0
        # A site profile is an optional, cross-session workplace-floor fingerprint.
        self.company_site_wifi: Set[str] = set()
        self.company_site_cells: Set[int] = set()
        self.company_site_ble: Set[str] = set()
        self._company_site_ble_seen = False
        self._company_site_wifi_attached_seen = False
        self._company_site_wifi_detached_seen = False
        self._last_wifi_evaluated_scan_ms = -1
        self._company_site_cell_attached_seen = False
        self._company_site_cell_detached = False
        self._company_site_cell_detach_streak = 0
        self._company_site_cell_attach_streak = 0
        self._last_cell_evaluated_sample_ms = -1

    def import_company_site_fingerprint(self, body: dict) -> bool:
        """Load a locally built company profile; returns whether it has WiFi."""
        company = body.get("company", body)
        wifi = company.get("wifi", {}) if isinstance(company, dict) else {}
        cell = company.get("cell", {}) if isinstance(company, dict) else {}
        ble = company.get("ble", {}) if isinstance(company, dict) else {}
        self.company_site_wifi = {
            str(x).lower() for x in wifi.get("bssids", []) if str(x).strip()
        }
        self.company_site_cells = {
            int(x) for x in cell.get("cell_ids", []) if str(x).strip() and int(x) != 0
        }
        self.company_site_ble = {
            str(x).lower() for x in ble.get("macs", []) if str(x).strip()
        }
        self._company_site_wifi_attached_seen = False
        self._company_site_wifi_detached_seen = False
        self._last_wifi_evaluated_scan_ms = -1
        self._company_detach_streak = 0
        self._company_detach_since = 0
        self._company_attach_streak = 0
        self._company_attach_since = 0
        self._company_site_cell_attached_seen = False
        self._company_site_cell_detached = False
        self._company_site_cell_detach_streak = 0
        self._company_site_cell_attach_streak = 0
        self._last_cell_evaluated_sample_ms = -1
        return bool(self.company_site_wifi)

    def import_company_site_fingerprint_file(self, path: str) -> bool:
        with open(path, encoding="utf-8") as f:
            return self.import_company_site_fingerprint(json.load(f))

    def on_wifi_scan(self, scan: WifiScan) -> None:
        self.wifi_history.append(scan)
        self._trim()

    def on_cell(self, cell: CellSample) -> None:
        self.cell_history.append(cell)
        self._trim()
        if cell.cell_id == 0:
            return
        if self.stable_cell_id == 0:
            self.stable_cell_id = cell.cell_id
            self.stable_cell_since_ms = cell.t_ms
        elif cell.cell_id == self.stable_cell_id:
            if self.stable_cell_since_ms <= 0:
                self.stable_cell_since_ms = cell.t_ms

    def on_ble(self, ble: BleSample) -> None:
        self.ble_history.append(ble)
        self._trim()

    def observe_dwell(self, t_ms: int, home_inside: bool, company_inside: bool) -> None:
        latest = self.wifi_history[-1] if self.wifi_history else None
        self._update_dwell(self.home, t_ms, home_inside, latest)
        self._update_dwell(self.company, t_ms, company_inside, latest)

    def evaluate(self, t_ms: int) -> RadioSnapshot:
        out = RadioSnapshot(
            home_dwell_ready=self.home.ready,
            company_dwell_ready=self.company.ready,
        )
        cur = self.wifi_history[-1] if self.wifi_history else None
        fresh_wifi_scan = cur is not None and cur.t_ms != self._last_wifi_evaluated_scan_ms
        company_site_scan_observed = False
        if cur is None:
            out.reason = "no_wifi"
        else:
            out.n_strong = _n_strong(cur, self.cfg.rssi_strong)
            raw_home = False
            raw_co = False
            if self.home.ready:
                raw_home, out.jaccard_home = self._detach_dwell(self.home, cur)
            if self.company_site_wifi:
                raw_co, out.company_site_wifi_coverage, out.company_site_wifi_matches = (
                    self._detach_site_wifi(cur)
                )
                company_site_scan_observed = bool(_strong_set(cur, self.cfg.rssi_min))
                out.jaccard_company = out.company_site_wifi_coverage
                out.reason = "company_site_wifi"
            elif self.company.ready:
                raw_co, out.jaccard_company = self._detach_dwell(self.company, cur)
            if not self.home.ready and not self.company.ready and not self.company_site_wifi:
                churn, out.jaccard_churn = self._temporal_churn(t_ms, cur)
                raw_home = churn
                raw_co = churn
            elif not self.home.ready:
                raw_home, out.jaccard_churn = self._temporal_churn(t_ms, cur)
            elif not self.company.ready and not self.company_site_wifi:
                raw_co, out.jaccard_churn = self._temporal_churn(t_ms, cur)

            out.wifi_home_detach = self._latch(
                raw_home,
                "_home_detach_streak",
                "_home_detach_since",
                t_ms,
                fresh_wifi_scan,
                self.cfg.detach_confirm_scans,
                self.cfg.detach_confirm_ms,
            )
            company_detach_scans = (
                self.cfg.site_wifi_detach_confirm_scans
                if self.company_site_wifi
                else self.cfg.detach_confirm_scans
            )
            company_detach_confirmed = self._latch(
                raw_co,
                "_company_detach_streak",
                "_company_detach_since",
                t_ms,
                fresh_wifi_scan and (not self.company_site_wifi or company_site_scan_observed),
                company_detach_scans,
                (
                    self.cfg.site_wifi_detach_confirm_ms
                    if self.company_site_wifi
                    else self.cfg.detach_confirm_ms
                ),
            )
            if self.company_site_wifi:
                if company_detach_confirmed:
                    self._company_site_wifi_detached_seen = True
                out.wifi_company_detach = self._company_site_wifi_detached_seen
            else:
                out.wifi_company_detach = company_detach_confirmed

            raw_att_h = self._attach_signal(t_ms, cur, self.home, out.jaccard_home)
            raw_att_c = (
                self._company_site_wifi_detached_seen
                and company_site_scan_observed
                and out.company_site_wifi_coverage >= self.cfg.site_wifi_attach_recall
                if self.company_site_wifi
                else self._attach_signal(t_ms, cur, self.company, out.jaccard_company)
            )
            # Temporal surge applies to both sides when soft missing.
            surge = self._n_strong_surge(t_ms, cur) if not self.company_site_wifi else False
            if surge:
                raw_att_h = True
                raw_att_c = True
            out.wifi_home_attach = self._latch(
                raw_att_h,
                "_home_attach_streak",
                "_home_attach_since",
                t_ms,
                fresh_wifi_scan,
                self.cfg.detach_confirm_scans,
                self.cfg.detach_confirm_ms,
            )
            company_attach_scans = (
                self.cfg.site_wifi_attach_confirm_scans
                if self.company_site_wifi
                else self.cfg.detach_confirm_scans
            )
            out.wifi_company_attach = self._latch(
                raw_att_c,
                "_company_attach_streak",
                "_company_attach_since",
                t_ms,
                fresh_wifi_scan and (not self.company_site_wifi or company_site_scan_observed),
                company_attach_scans,
                (
                    self.cfg.site_wifi_attach_confirm_ms
                    if self.company_site_wifi
                    else self.cfg.detach_confirm_ms
                ),
            )
            # Attach and detach are mutually exclusive for gating.
            if out.wifi_home_attach:
                out.wifi_home_detach = False
                self._home_detach_streak = 0
                self._home_detach_since = 0
            if out.wifi_company_attach:
                out.wifi_company_detach = False
                self._company_detach_streak = 0
                self._company_detach_since = 0
                self._company_site_wifi_detached_seen = False
            self._last_wifi_evaluated_scan_ms = cur.t_ms
            if self.company_site_wifi:
                # Preserve raw recall in company_site_wifi_coverage, but expose
                # only the confirmed state to HSMM to avoid reusing a stale low
                # scan as strong evidence on every engine tick.
                out.jaccard_company = 0.0 if out.wifi_company_detach else 1.0

        if self.cfg.ble_evidence_enabled:
            ble_raw, _ = self._ble_churn(t_ms)
            out.ble_home_detach = self._latch(ble_raw, "_ble_home_streak", "_ble_home_since", t_ms)
            if self.company_site_ble:
                site_ble = self._site_ble_matches(t_ms)
                out.company_site_ble_matches = site_ble
                if site_ble >= 2:
                    self._company_site_ble_seen = True
                ble_raw = self._company_site_ble_seen and site_ble == 0
            out.ble_company_detach = self._latch(
                ble_raw, "_ble_company_streak", "_ble_company_since", t_ms
            )

        cell_raw = self._cell_leave(t_ms)
        fresh_cell = bool(self.cell_history) and (
            self.cell_history[-1].t_ms != self._last_cell_evaluated_sample_ms
        )
        if fresh_cell:
            if cell_raw:
                self._cell_home_streak += 1
                if not self.company_site_cells:
                    self._cell_company_streak += 1
            else:
                self._cell_home_streak = 0
                if not self.company_site_cells:
                    self._cell_company_streak = 0
        out.cell_leave_home = self._cell_home_streak >= self.cfg.detach_confirm_scans
        out.cell_leave_company = (
            not self.company_site_cells
            and self._cell_company_streak >= self.cfg.detach_confirm_scans
        )
        if self.cell_history and self.company_site_cells:
            out.company_site_cell_match = self.cell_history[-1].cell_id in self.company_site_cells
            recent = [
                x
                for x in self.cell_history
                if x.cell_id and 0 <= t_ms - x.t_ms <= self.cfg.site_cell_window_ms
            ]
            matched = sum(x.cell_id in self.company_site_cells for x in recent)
            if recent:
                out.company_site_cell_match_ratio = matched / len(recent)
            observable = len(recent) >= self.cfg.site_cell_min_samples
            if fresh_cell and observable:
                if out.company_site_cell_match_ratio >= self.cfg.site_cell_attach_match_ratio:
                    self._company_site_cell_attached_seen = True
                raw_floor_detach = (
                    self._company_site_cell_attached_seen
                    and out.company_site_cell_match_ratio <= self.cfg.site_cell_detach_match_ratio
                )
                raw_floor_attach = (
                    self._company_site_cell_detached
                    and out.company_site_cell_match_ratio >= self.cfg.site_cell_attach_match_ratio
                )
                self._company_site_cell_detach_streak = (
                    self._company_site_cell_detach_streak + 1 if raw_floor_detach else 0
                )
                self._company_site_cell_attach_streak = (
                    self._company_site_cell_attach_streak + 1 if raw_floor_attach else 0
                )
                if self._company_site_cell_detach_streak >= self.cfg.site_cell_confirm_samples:
                    self._company_site_cell_detached = True
                    self._company_site_cell_attach_streak = 0
                if self._company_site_cell_attach_streak >= self.cfg.site_cell_confirm_samples:
                    self._company_site_cell_detached = False
                    self._company_site_cell_detach_streak = 0
                    self._company_site_cell_attach_streak = 0
            out.cell_leave_company = self._company_site_cell_detached
        if self.cell_history:
            self._last_cell_evaluated_sample_ms = self.cell_history[-1].t_ms

        if self.cell_history:
            cid = self.cell_history[-1].cell_id
            if cid and cid != self.stable_cell_id:
                if out.cell_leave_home or (
                    not self.company_site_cells and out.cell_leave_company
                ):
                    self.stable_cell_id = cid
                    self.stable_cell_since_ms = self.cell_history[-1].t_ms
                    self._cell_home_streak = 0
                    self._cell_company_streak = 0
                elif self.stable_cell_since_ms <= 0 or (
                    t_ms - self.stable_cell_since_ms
                ) < self.cfg.cell_stable_ms:
                    self.stable_cell_id = cid
                    self.stable_cell_since_ms = self.cell_history[-1].t_ms
                    self._cell_home_streak = 0
                    self._cell_company_streak = 0

        return out

    def export_soft_json(self) -> str:
        def side(st: _Dwell) -> dict:
            return {
                "ready": st.ready,
                "soft_set": sorted(st.soft_set),
                "n": len(st.soft_set),
            }

        return json.dumps({"home": side(self.home), "company": side(self.company)}, ensure_ascii=False)

    def _latch(
        self,
        raw: bool,
        streak_attr: str,
        since_attr: str,
        t_ms: int,
        fresh: bool = True,
        confirm_scans: Optional[int] = None,
        confirm_ms: Optional[int] = None,
    ) -> bool:
        confirm_scans = confirm_scans or self.cfg.detach_confirm_scans
        confirm_ms = confirm_ms if confirm_ms is not None else self.cfg.detach_confirm_ms
        streak = getattr(self, streak_attr)
        since = getattr(self, since_attr)
        if not fresh:
            return streak >= confirm_scans
        if raw:
            if streak == 0:
                since = t_ms
            streak += 1
            setattr(self, streak_attr, streak)
            setattr(self, since_attr, since)
            return streak >= confirm_scans or (
                since > 0 and (t_ms - since) >= confirm_ms
            )
        setattr(self, streak_attr, 0)
        setattr(self, since_attr, 0)
        return False

    def _trim(self) -> None:
        cap = self.cfg.history_cap
        while len(self.wifi_history) > cap:
            self.wifi_history.popleft()
        while len(self.cell_history) > cap * 4:
            self.cell_history.popleft()
        while len(self.ble_history) > cap * 20:
            self.ble_history.popleft()

    def _update_dwell(self, st: _Dwell, t_ms: int, inside: bool, latest: Optional[WifiScan]) -> None:
        if inside:
            if not st.was_inside:
                st.inside_since_ms = t_ms
            st.was_inside = True
            st.last_inside_ms = t_ms
            if latest is not None:
                for ap in latest.aps:
                    if not ap.bssid or ap.rssi < self.cfg.rssi_min:
                        continue
                    b = ap.bssid.lower()
                    st.bssid_count[b] = st.bssid_count.get(b, 0) + 1
                    dq = st.bssid_rssi.setdefault(b, deque(maxlen=32))
                    dq.append(ap.rssi)
            dwell_ms = t_ms - st.inside_since_ms if st.inside_since_ms else 0
            st.inside_accum_ms = max(st.inside_accum_ms, dwell_ms)
            if st.inside_accum_ms >= self.cfg.dwell_learn_ms or st.ready:
                self._rebuild_soft(st)
        else:
            st.was_inside = False
            st.inside_since_ms = 0

    def _rebuild_soft(self, st: _Dwell) -> None:
        ranked = sorted(
            ((c, b) for b, c in st.bssid_count.items() if c >= self.cfg.dwell_min_count),
            reverse=True,
        )
        soft = {b for _, b in ranked[: self.cfg.dwell_set_size]}
        st.soft_set = soft
        st.ready = len(soft) >= 2

    def _detach_dwell(self, st: _Dwell, cur: WifiScan) -> Tuple[bool, float]:
        cur_set = _strong_set(cur, self.cfg.rssi_min)
        jac = _jaccard(cur_set, st.soft_set)
        if jac < self.cfg.jaccard_detach:
            return True, jac
        # RSSI drop on soft BSSIDs
        drops = 0
        checked = 0
        cur_map = {ap.bssid.lower(): ap.rssi for ap in cur.aps if ap.bssid}
        for b in st.soft_set:
            hist = st.bssid_rssi.get(b)
            if not hist:
                continue
            med = sorted(hist)[len(hist) // 2]
            checked += 1
            now = cur_map.get(b, -127)
            if med - now >= self.cfg.rssi_drop_db:
                drops += 1
        if checked >= 2 and drops >= max(1, checked // 2):
            return True, jac
        return False, jac

    def _detach_site_wifi(self, cur: WifiScan) -> Tuple[bool, float, int]:
        current = _strong_set(cur, self.cfg.rssi_min)
        if not current:
            return False, 0.0, 0
        matches = len(current & self.company_site_wifi)
        coverage = matches / len(self.company_site_wifi)
        if coverage >= self.cfg.site_wifi_attach_recall:
            self._company_site_wifi_attached_seen = True
        detached = (
            self._company_site_wifi_attached_seen
            and coverage <= self.cfg.site_wifi_detach_recall
        )
        return detached, coverage, matches

    def _site_ble_matches(self, t_ms: int) -> int:
        current = {
            b.mac.lower()
            for b in self.ble_history
            if t_ms - 30_000 <= b.t_ms <= t_ms and b.mac and b.rssi >= self.cfg.rssi_min
        }
        return len(current & self.company_site_ble)

    def _scan_near(self, t_ms: int, ago_ms: int) -> Optional[WifiScan]:
        target = t_ms - ago_ms
        best = None
        best_dt = 10**18
        for s in self.wifi_history:
            dt = abs(s.t_ms - target)
            if dt < best_dt and s.t_ms <= t_ms:
                best = s
                best_dt = dt
        if best is None or best_dt > ago_ms * 0.75:
            return None
        return best

    def _temporal_churn(self, t_ms: int, cur: WifiScan) -> Tuple[bool, float]:
        base = self._scan_near(t_ms, self.cfg.churn_baseline_ms)
        if base is None:
            return False, 1.0
        jac = _jaccard(_strong_set(cur, self.cfg.rssi_min), _strong_set(base, self.cfg.rssi_min))
        return jac < self.cfg.jaccard_churn, jac

    def _n_strong_surge(self, t_ms: int, cur: WifiScan) -> bool:
        base = self._scan_near(t_ms, self.cfg.churn_baseline_ms)
        if base is None:
            return False
        n0 = _n_strong(base, self.cfg.rssi_strong)
        n1 = _n_strong(cur, self.cfg.rssi_strong)
        if n1 >= n0 + self.cfg.attach_n_strong_delta:
            return True
        if n0 <= 2 and n1 >= self.cfg.attach_n_strong_abs:
            return True
        return False

    def _attach_signal(self, t_ms: int, cur: WifiScan, st: _Dwell, jac: float) -> bool:
        if st.ready and jac >= self.cfg.jaccard_attach and len(_strong_set(cur, self.cfg.rssi_min)) >= 2:
            return True
        return False

    def _ble_churn(self, t_ms: int) -> Tuple[bool, float]:
        def macs(window_ms: int, end_ms: int) -> Set[str]:
            start = end_ms - window_ms
            out: Set[str] = set()
            for b in self.ble_history:
                if start <= b.t_ms <= end_ms and b.mac and b.rssi >= self.cfg.rssi_min:
                    out.add(b.mac.lower())
            return out

        cur = macs(30_000, t_ms)
        base = macs(30_000, t_ms - self.cfg.churn_baseline_ms)
        if not cur or not base:
            return False, 1.0
        jac = _jaccard(cur, base)
        return jac < self.cfg.jaccard_churn, jac

    def _cell_leave(self, t_ms: int) -> bool:
        if not self.cell_history:
            return False
        cur = self.cell_history[-1]
        if cur.cell_id == 0 or cur.cell_id == self.stable_cell_id:
            return False
        if self.stable_cell_since_ms <= 0 or (t_ms - self.stable_cell_since_ms) < self.cfg.cell_stable_ms:
            return False
        return True


def _open_csv(path: str):
    # BLE dumps may mix encodings; replace keeps load resilient.
    return open(path, newline="", encoding="utf-8", errors="replace")


def load_wifi_scans(dirs: Iterable[str]) -> List[WifiScan]:
    """Load wifi_data_*.csv; group rows sharing wallTsMs into one scan."""
    rows: List[Tuple[int, str, int]] = []
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "wifi_data_*.csv"))):
            with _open_csv(path) as f:
                for row in csv.DictReader(f):
                    try:
                        t = int(float(row["wallTsMs"]))
                        bssid = (row.get("bssid") or "").strip()
                        rssi = int(float(row.get("rssi") or -127))
                    except (KeyError, ValueError, TypeError):
                        continue
                    if bssid:
                        rows.append((t, bssid, rssi))
    rows.sort(key=lambda x: x[0])
    scans: List[WifiScan] = []
    cur_t = None
    cur_aps: List[WifiAp] = []
    for t, bssid, rssi in rows:
        if cur_t is None:
            cur_t = t
        if t != cur_t:
            scans.append(WifiScan(t_ms=cur_t, aps=cur_aps))
            cur_t = t
            cur_aps = []
        cur_aps.append(WifiAp(bssid=bssid, rssi=rssi))
    if cur_t is not None:
        scans.append(WifiScan(t_ms=cur_t, aps=cur_aps))
    return scans


def load_cell_samples(dirs: Iterable[str]) -> List[CellSample]:
    out: List[CellSample] = []
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "cell_data_*.csv"))):
            with _open_csv(path) as f:
                for row in csv.DictReader(f):
                    try:
                        t = int(float(row["wallTsMs"]))
                        cid = int(float(row.get("cellId") or 0))
                        rssi = int(float(row.get("signalIntensity") or -127))
                    except (KeyError, ValueError, TypeError):
                        continue
                    out.append(CellSample(t_ms=t, cell_id=cid, rssi=rssi))
    out.sort(key=lambda x: x.t_ms)
    return out


def load_ble_samples(dirs: Iterable[str]) -> List[BleSample]:
    out: List[BleSample] = []
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "ble_data_*.csv"))):
            with _open_csv(path) as f:
                for row in csv.DictReader(f):
                    try:
                        t = int(float(row["wallTsMs"]))
                        mac = (row.get("mac") or "").strip()
                        rssi = int(float(row.get("rssi") or -127))
                    except (KeyError, ValueError, TypeError):
                        continue
                    if mac:
                        out.append(BleSample(t_ms=t, mac=mac, rssi=rssi))
    out.sort(key=lambda x: x.t_ms)
    return out


class RadioFeed:
    """Advance RadioEvidence up to GPS tick time from preloaded streams."""

    def __init__(
        self,
        wifi: List[WifiScan],
        cells: List[CellSample],
        ble: List[BleSample],
        radio: Optional[RadioEvidence] = None,
    ):
        self.wifi = wifi
        self.cells = cells
        self.ble = ble
        self.radio = radio or RadioEvidence()
        self.wi = 0
        self.ci = 0
        self.bi = 0

    def advance(self, t_ms: int) -> RadioSnapshot:
        while self.wi < len(self.wifi) and self.wifi[self.wi].t_ms <= t_ms:
            self.radio.on_wifi_scan(self.wifi[self.wi])
            self.wi += 1
        while self.ci < len(self.cells) and self.cells[self.ci].t_ms <= t_ms:
            self.radio.on_cell(self.cells[self.ci])
            self.ci += 1
        while self.bi < len(self.ble) and self.ble[self.bi].t_ms <= t_ms:
            self.radio.on_ble(self.ble[self.bi])
            self.bi += 1
        return self.radio.evaluate(t_ms)
