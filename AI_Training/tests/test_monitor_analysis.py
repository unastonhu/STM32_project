from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from analyze_monitor_log import analyze_messages


class MonitorAnalysisTest(unittest.TestCase):
    def test_false_online_bme_is_reported(self) -> None:
        slow = {
            "cmd": "SLOW",
            "ts": 100,
            "hx": {"w": 8.0, "s": 1},
            "sgp": {"v": 100, "r": 32000, "s": 1},
            "env": {
                "at": 30.0,
                "ah": 50.0,
                "tv": 40,
                "co": 430,
                "s": 1,
            },
            "bme": {
                "t": 0.0,
                "h": 0.0,
                "p": 0.0,
                "g": 0.0,
                "s": 1,
            },
        }
        report = analyze_messages([slow])
        errors = [
            issue["message"]
            for issue in report["issues"]
            if issue["severity"] == "error"
        ]
        self.assertTrue(any("BME688" in message for message in errors))
        self.assertFalse(report["valid_for_freshness_training"])

    def test_real_bme_values_are_not_false_positive(self) -> None:
        slow = {
            "cmd": "SLOW",
            "ts": 100,
            "hx": {"w": 8.0, "s": 1},
            "sgp": {"v": 100, "r": 32000, "s": 1},
            "env": {
                "at": 30.0,
                "ah": 50.0,
                "tv": 40,
                "co": 430,
                "s": 1,
            },
            "bme": {
                "t": 30.0,
                "h": 50.0,
                "p": 1010.0,
                "g": 100000.0,
                "s": 1,
            },
        }
        report = analyze_messages([slow])
        errors = [
            issue
            for issue in report["issues"]
            if issue["severity"] == "error"
        ]
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
