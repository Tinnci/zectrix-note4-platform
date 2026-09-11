import csv
import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("display_telemetry", Path(__file__).with_name("display-telemetry.py"))
telemetry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(telemetry)

CAPTURE = """Zectrix maintenance CLI
zectrix> display telemetry
# epd next=7 latest=7 lost=2 frames=1
frame,7,1000000,2,0,0,7,16,31,16,8,0,2,180000,80000,80000,65,32,1
env,7,0,5000,3800,2000,0,512
debt,7,0,30,600,30,600,0
zectrix>
"""


class TelemetryTest(unittest.TestCase):
    def test_csv_and_real_zero_temperature(self):
        frames, losses = telemetry.parse_capture(CAPTURE.splitlines())
        self.assertEqual(losses, [2])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["temperature_centi_c"], 0)
        self.assertEqual(frames[0]["panel_temperature_centi_c"], 0)
        self.assertEqual(frames[0]["energy_uj"], "")
        stream = io.StringIO()
        writer = csv.DictWriter(stream, telemetry.FIELDNAMES)
        writer.writeheader()
        writer.writerows(frames)
        row = next(csv.DictReader(io.StringIO(stream.getvalue())))
        self.assertEqual(row["ram_bytes"], "32")
        self.assertEqual(row["black_to_white"], "0")

    def test_unknown_is_empty_and_stale_keeps_its_age(self):
        capture = CAPTURE.replace(",0,7,16,31", ",0,0,16,31").replace("env,7,0,5000,3800,2000,0,512", "env,7,0,4294967295,3800,65000,0,384")
        row = telemetry.parse_capture(capture.splitlines())[0][0]
        for field in ("black_to_white", "white_to_black", "spi_bytes", "panel_temperature_centi_c", "temperature_centi_c"):
            self.assertEqual(row[field], "")
        self.assertEqual(row["battery_age_ms"], 65000)
        self.assertEqual(row["duration_us"], 180000)

    def test_reject_partial_or_mixed_frames(self):
        for capture in (CAPTURE.replace("debt,7", "debt,8"), CAPTURE.replace("env,7,0,5000,3800,2000,0,512\n", ""),
                        CAPTURE.replace("180000", "garbled"), CAPTURE.replace(",600,0\n", ",600\n")):
            with self.assertRaises(ValueError):
                telemetry.parse_capture(capture.splitlines())

    def test_empty_batch_and_terminal_escape(self):
        self.assertEqual(telemetry.parse_capture(["# epd next=0 latest=0 lost=0 frames=0"])[0], [])
        capture = CAPTURE.replace("frame,7", "\x1b[32mframe,7").replace("32,1\n", "32,1\x1b[0m\n")
        self.assertEqual(len(telemetry.parse_capture(capture.splitlines())[0]), 1)


if __name__ == "__main__":
    unittest.main()
