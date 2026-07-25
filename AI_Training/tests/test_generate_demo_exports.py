from pathlib import Path
import tempfile
import unittest

import numpy as np

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from dataset import build_windows, parse_export_file
from generate_demo_exports import generate_demo_text


class GenerateDemoExportsTest(unittest.TestCase):
    def test_generated_demo_matches_firmware_export_contract(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "demo.txt"
        path.write_text(generate_demo_text(), encoding="utf-8")

        groups = parse_export_file(path)
        dataset = build_windows(groups)

        self.assertEqual(len(groups), 6)
        self.assertEqual(dataset.x.shape, (54, 60, 5))
        self.assertEqual(
            [int(np.count_nonzero(dataset.y == label)) for label in range(3)],
            [18, 18, 18],
        )
        self.assertTrue(np.all(np.isfinite(dataset.x)))
        self.assertTrue(np.all(dataset.x >= 0.0))

    def test_too_short_group_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "至少需要 60 秒"):
            generate_demo_text(seconds_per_group=59)


if __name__ == "__main__":
    unittest.main()
