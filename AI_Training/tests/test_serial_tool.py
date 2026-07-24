from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from serial_tool import ExportCapture, json_command, label_value


class SerialToolTest(unittest.TestCase):
    def test_json_command_is_compact(self) -> None:
        self.assertEqual(
            json_command(
                "SAMPLE_ADD", start=100, end=200, label=1
            ),
            '{"cmd":"SAMPLE_ADD","start":100,"end":200,"label":1}',
        )

    def test_export_capture_ignores_fast_slow(self) -> None:
        capture = ExportCapture(4)
        lines = [
            '{"cmd":"FAST","dist":1}',
            "#EXPORT_CHUNK,id=4,seq=0,label=2,model=1,"
            "start=100,end=160",
            "timestamp,uptime_ms,sgp40_raw,tvoc,eco2,bme688_gas,"
            "weight,valid_mask,door_closed",
            "100,1000,1,2,3,4,5,31,1",
            '{"cmd":"SLOW","ts":100}',
            "#EXPORT_END,id=4,records=1,invalid=0",
        ]
        for line in lines:
            capture.feed(line)

        self.assertTrue(capture.finished)
        self.assertEqual(len(capture.lines), 4)
        self.assertNotIn("FAST", "".join(capture.lines))

    def test_export_error_finishes_capture(self) -> None:
        capture = ExportCapture(9)
        capture.feed(
            '{"cmd":"EXPORT_ERROR","id":9,'
            '"reason":"sample_not_found"}'
        )
        self.assertTrue(capture.finished)
        self.assertEqual(capture.error, "sample_not_found")

    def test_label_aliases(self) -> None:
        self.assertEqual(label_value("fresh"), 0)
        self.assertEqual(label_value("not_fresh"), 1)
        self.assertEqual(label_value("spoiled"), 2)
        self.assertEqual(label_value("2"), 2)


if __name__ == "__main__":
    unittest.main()
