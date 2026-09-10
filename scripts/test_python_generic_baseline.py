"""Parity checks for the Python raw-data replay mirror of the generic C++ baseline."""

from datetime import datetime, timedelta
import unittest

from commute_baseline.hsmm import LeaveHsmm, LeaveObservation


class GenericHsmmParityTest(unittest.TestCase):
    def test_lower_platform_is_not_a_generic_hsmm_emission(self):
        theta = {
            "w_walk": 1.0,
            "w_pdr": 0.85,
            "w_geo": 0.85,
            "w_wifi": 0.61,
            "w_cell": 0.49,
            "w_ble": 0.0,
            "w_time": 0.0,
            "w_baro": 0.85,
        }
        start = datetime(2026, 1, 1, 12, 0, 0)
        common = dict(
            walking=1.0,
            pdr_outbound=0.7,
            inside=True,
            relation_known=True,
            baro_available=True,
            baro_descending=0.8,
        )
        without_platform = LeaveHsmm()
        with_platform = LeaveHsmm()
        without_platform.step(LeaveObservation(**common, baro_lower_platform=0.0), start, theta)
        with_platform.step(LeaveObservation(**common, baro_lower_platform=1.0), start, theta)
        a = without_platform.step(
            LeaveObservation(**common, baro_lower_platform=0.0), start + timedelta(seconds=5), theta
        )
        b = with_platform.step(
            LeaveObservation(**common, baro_lower_platform=1.0), start + timedelta(seconds=5), theta
        )
        self.assertEqual(a.probability, b.probability)


if __name__ == "__main__":
    unittest.main()
