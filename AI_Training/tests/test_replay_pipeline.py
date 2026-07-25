from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from dataset import ExportGroup, ExportRecord
from replay_pipeline import (
    apply_sample_edits,
    compare_scenarios,
    parse_relabel,
)


def make_group(group_id: int, label: int) -> ExportGroup:
    return ExportGroup(
        source=f"source_{group_id}",
        group_id=group_id,
        label=label,
        model_version=3,
        records=[
            ExportRecord(
                timestamp=100,
                uptime_ms=1000,
                raw=np.ones(5, dtype=np.float32),
                valid_mask=31,
                door_closed=1,
            )
        ],
    )


class ReplayPipelineTest(unittest.TestCase):
    def test_delete_and_relabel_do_not_mutate_source_groups(self) -> None:
        groups = [make_group(1, 0), make_group(2, 1)]

        edited = apply_sample_edits(groups, {1}, {2: 2})

        self.assertEqual([group.group_id for group in edited], [2])
        self.assertEqual(edited[0].label, 2)
        self.assertEqual(groups[1].label, 1)

    def test_unknown_group_edit_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "找不到样本组"):
            apply_sample_edits([make_group(1, 0)], {99}, {})

    def test_relabel_parser(self) -> None:
        self.assertEqual(parse_relabel("7:2"), (7, 2))

    def test_scenario_comparison_exposes_edit_delta(self) -> None:
        baseline = {
            "replay_available": True,
            "dataset": {
                "groups_total": 6,
                "windows_total": 54,
                "windows_per_label": {"0": 18, "1": 18, "2": 18},
            },
            "prototype": {
                "valid_label_mask": 7,
                "window_counts": [18, 18, 18],
                "scale": {"min": 0.1, "max": 1.0, "mean": 0.5},
            },
        }
        edited = {
            "replay_available": True,
            "dataset": {
                "groups_total": 5,
                "windows_total": 45,
                "windows_per_label": {"0": 9, "1": 18, "2": 18},
            },
            "prototype": {
                "valid_label_mask": 7,
                "window_counts": [9, 18, 18],
                "scale": {"min": 0.1, "max": 0.9, "mean": 0.4},
            },
        }

        comparison = compare_scenarios(baseline, edited)

        self.assertEqual(comparison["groups_total_delta"], -1)
        self.assertEqual(comparison["windows_total_delta"], -9)
        self.assertEqual(
            comparison["windows_per_label_delta"],
            {"0": -9, "1": 0, "2": 0},
        )
        self.assertTrue(comparison["prototype_changed"])


if __name__ == "__main__":
    unittest.main()
