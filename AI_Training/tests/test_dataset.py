from pathlib import Path
import tempfile
import unittest

import numpy as np

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from dataset import build_windows, parse_export_file, split_by_group
from train_feature_extractor import write_preprocess_header


def make_record(second: int, valid_mask: int = 31, door: int = 1) -> str:
    return (
        f"{1000 + second},{second * 1000},"
        f"{100 + second},10,400,50000,200,{valid_mask},{door}\n"
    )


class DatasetTest(unittest.TestCase):
    def write_export(self, text: str) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "capture.txt"
        path.write_text(text, encoding="utf-8")
        return path

    def test_interleaved_json_and_repeated_chunks(self) -> None:
        first = "".join(make_record(index) for index in range(40))
        second = "".join(make_record(index) for index in range(40, 75))
        path = self.write_export(
            '{"cmd":"FAST","dist":1}\n'
            "#EXPORT_CHUNK,id=7,seq=0,label=1,model=1\n"
            "timestamp,uptime_ms,sgp40_raw,tvoc,eco2,bme688_gas,"
            "weight,valid_mask,door_closed\n"
            f"{first}"
            '{"cmd":"SLOW","ts":1040}\n'
            "#EXPORT_CHUNK,id=7,seq=1,label=1,model=1\n"
            f"{second}"
            "#EXPORT_END,id=7,records=75,invalid=0\n"
        )

        groups = parse_export_file(path)
        dataset = build_windows(groups)

        self.assertEqual(len(groups), 1)
        self.assertEqual(len(groups[0].records), 75)
        self.assertEqual(dataset.x.shape, (2, 60, 5))
        self.assertTrue(np.all(dataset.y == 1))

    def test_invalid_or_open_window_is_rejected(self) -> None:
        rows = "".join(
            make_record(
                index,
                valid_mask=0 if index < 7 else 31,
                door=0 if index < 7 else 1,
            )
            for index in range(60)
        )
        path = self.write_export(
            "#EXPORT_CHUNK,id=1,seq=0,label=0,model=1\n"
            f"{rows}"
            "#EXPORT_END,id=1,records=60,invalid=0\n"
        )

        dataset = build_windows(parse_export_file(path))
        self.assertEqual(dataset.x.shape[0], 0)

    def test_group_split_never_leaks_same_group(self) -> None:
        text = ""
        for group_id in range(1, 7):
            label = (group_id - 1) % 3
            text += (
                f"#EXPORT_CHUNK,id={group_id},seq=0,"
                f"label={label},model=1\n"
            )
            text += "".join(make_record(index) for index in range(60))
            text += (
                f"#EXPORT_END,id={group_id},records=60,invalid=0\n"
            )

        dataset = build_windows(parse_export_file(self.write_export(text)))
        train, validation = split_by_group(dataset, 0.5, seed=42)
        train_groups = set(dataset.group_keys[train])
        validation_groups = set(dataset.group_keys[validation])

        self.assertFalse(train_groups & validation_groups)
        self.assertGreater(len(validation), 0)

    def test_updated_group_replaces_previous_export(self) -> None:
        old_rows = "".join(make_record(index) for index in range(60))
        new_rows = "".join(make_record(index) for index in range(100, 160))
        path = self.write_export(
            "#EXPORT_CHUNK,id=3,seq=0,label=0,model=1,"
            "start=1000,end=1059\n"
            f"{old_rows}"
            "#EXPORT_END,id=3,records=60,invalid=0\n"
            "#EXPORT_CHUNK,id=3,seq=0,label=2,model=1,"
            "start=1100,end=1159\n"
            f"{new_rows}"
            "#EXPORT_END,id=3,records=60,invalid=0\n"
        )

        groups = parse_export_file(path)
        self.assertEqual(groups[0].label, 2)
        self.assertEqual(groups[0].start_timestamp, 1100)
        self.assertEqual(len(groups[0].records), 60)
        self.assertEqual(groups[0].records[0].uptime_ms, 100000)

    def test_generated_c_float_literals_are_valid(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "config.h"
        write_preprocess_header(
            path,
            np.asarray([1, 2, 3, 4, 5], dtype=np.float32),
            np.ones(5, dtype=np.float32),
        )

        text = path.read_text(encoding="utf-8")
        self.assertIn("1.0f", text)
        self.assertNotIn(" 1f", text)


if __name__ == "__main__":
    unittest.main()
