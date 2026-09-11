"""Exercise artifact reporting across current, expanded and asymmetric layouts."""

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from firmware_budget import boot_partition_matches, inspect_budget, read_partitions


class FirmwareBudgetTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "config").mkdir()
        self.write_json("project_description.json", {
            "target": "esp32s3", "app_bin": "product.bin", "app_elf": "product.elf",
            "build_components": [],
        })
        self.write_json("flasher_args.json", {
            "flash_settings": {"flash_size": "16MB"},
            "partition-table": {"offset": "0x8000", "file": "layout.bin"},
        })
        self.write_json("config/sdkconfig.json", {"COMPILER_OPTIMIZATION_SIZE": True})
        with (self.root / "product.bin").open("wb") as image:
            image.truncate(3_139_120)
        self.layout()

    def write_json(self, name, value):
        (self.root / name).write_text(json.dumps(value))

    def layout(self, sizes=(0x300000, 0x300000, 0x300000)):
        entries = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000)]
        cursor = 0x10000
        for name, subtype, size in zip(("factory", "ota_0", "ota_1"), (0, 0x10, 0x11), sizes):
            entries.append((name, 0, subtype, cursor, size))
            cursor += size
        entries.extend([("otadata", 1, 0, cursor, 0x2000),
                        ("books", 1, 0x82, cursor + 0x2000, 0x400000)])
        data = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, address, size,
                                    name.encode(), 0)
                        for name, kind, subtype, address, size in entries)
        (self.root / "layout.bin").write_bytes(data + bytes([0xFF]) * 32)

    def test_current_layout_and_binary_size(self):
        report = inspect_budget(self.root)
        self.assertEqual(report["application_free_bytes"], 6608)
        self.assertEqual(report["reader_font_bytes"], 0)
        self.assertEqual(report["unallocated_regions"], [{"offset": 0xD12000, "size": 0x2EE000}])
        self.assertEqual(report["partitions"][-1]["offset"], 0x912000)
        with (self.root / "product.bin").open("wb") as image:
            image.truncate(1000)
        self.assertEqual(inspect_budget(self.root)["application_bytes"], 1000)

    def test_expanded_slots_are_derived(self):
        self.layout((0x3F0000,) * 3)
        report = inspect_budget(self.root)
        self.assertEqual([p["offset"] for p in report["application_slots"]], [0x10000, 0x400000, 0x7F0000])
        self.assertEqual(report["minimum_slot_bytes"], 0x3F0000)
        self.assertEqual(report["partitions"][-1]["offset"], 0xBE2000)
        self.assertEqual(report["unallocated_bytes"], 0x1E000)

    def test_asymmetric_slots_use_smallest_capacity(self):
        self.layout((0x200000, 0x400000, 0x400000))
        report = inspect_budget(self.root)
        self.assertEqual(report["minimum_slot_bytes"], 0x200000)
        self.assertEqual(report["application_free_bytes"], 0x200000 - 3_139_120)
        self.assertGreater(report["application_slots"][1]["free_bytes"], 0)

    def test_table_bounds(self):
        self.layout((0x400000,) * 3)
        with self.assertRaisesRegex(ValueError, "exceeds Flash"):
            inspect_budget(self.root)
        (self.root / "layout.bin").write_bytes(bytes(31))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            read_partitions(self.root / "layout.bin")
        (self.root / "layout.bin").write_bytes(bytes(32))
        with self.assertRaisesRegex(ValueError, "Invalid"):
            read_partitions(self.root / "layout.bin")

    def test_linked_font_uses_built_symbol(self):
        self.write_json("project_description.json", {
            "target": "esp32s3", "app_bin": "product.bin", "app_elf": "product.elf",
            "build_components": ["zectrix_reader"],
        })
        (self.root / "CMakeCache.txt").write_text("CMAKE_NM:FILEPATH=/toolchain/nm\n")
        with patch("firmware_budget.subprocess.run") as run:
            run.return_value.stdout = "unrelated T 4000 12\nzectrix_reader_font_data R 3c000000 12345\n"
            self.assertEqual(inspect_budget(self.root)["reader_font_bytes"], 0x12345)
            self.assertEqual(run.call_args.args[0][0], "/toolchain/nm")

    def test_boot_log_requires_actual_addresses(self):
        books = read_partitions(self.root / "layout.bin")[-1]
        log = "I (50) boot: 6 books Unknown data 01 82 00912000 00400000\n"
        self.assertTrue(boot_partition_matches(log, books))
        self.assertFalse(boot_partition_matches(log.replace("00912000", "00BE2000"), books))
        self.assertFalse(boot_partition_matches(log.replace("00400000", "00200000"), books))
        self.assertFalse(boot_partition_matches("books 00912000 00400000", books))

    def test_cli_report(self):
        output = self.root / "report.json"
        result = subprocess.run([sys.executable, str(Path(__file__).with_name("firmware_budget.py")),
                                 str(self.root), "--output", str(output)],
                                check=True, capture_output=True, text=True)
        self.assertIn("6,608", result.stdout)
        self.assertEqual(json.loads(output.read_text())["optimization"], "size")


if __name__ == "__main__":
    unittest.main()
