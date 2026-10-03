"""Exercise firmware download contents, data preservation and failed packaging."""

import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import firmware_package as package


class FirmwarePackageTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "downloads"
        self.full, self.minimal = (self.root / name for name in ("full", "minimal"))
        for name in package.NOTICES + ("managed_components/espressif__esp_codec_dev/LICENSE",):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Fixture license\n")
        self.patch = patch.object(package, "ROOT", self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        for directory in (self.full, self.minimal):
            (directory / "config").mkdir(parents=True)
            (directory / "application.bin").write_bytes(b"firmware" * 64)
            (directory / "boot.bin").write_bytes(b"boot")
            (directory / "ota.bin").write_bytes(bytes([255]) * 8192)
            partitions = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000),
                          ("factory", 0, 0, 0x10000, 0x300000), ("ota_0", 0, 0x10, 0x310000, 0x300000),
                          ("ota_1", 0, 0x11, 0x610000, 0x300000), ("otadata", 1, 0, 0x910000, 0x2000),
                          ("books", 1, 0x82, 0x912000, 0x400000)]
            (directory / "table.bin").write_bytes(b"".join(
                struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode(), 0)
                for name, kind, subtype, offset, size in partitions))
            self.write(directory, "project_description.json", {
                "target": "esp32s3", "app_bin": "application.bin", "build_components": [],
                "project_version": "v1.0.0-128-g108e96d", "idf_path": str(self.root),
            })
            self.write(directory, "config/sdkconfig.json", {"COMPILER_OPTIMIZATION_SIZE": True})
            flash = {role: {"offset": address, "file": name} for role, address, name in (
                ("bootloader", "0x0", "boot.bin"), ("partition-table", "0x8000", "table.bin"),
                ("app", "0x10000", "application.bin"), ("otadata", "0x910000", "ota.bin"))}
            flash["flash_files"] = {item["offset"]: item["file"] for item in flash.values()}
            flash["flash_settings"] = {"flash_size": "16MB"}
            flash["write_flash_args"] = ["--flash_mode", "dio", "--flash_size", "16MB"]
            self.write(directory, "flasher_args.json", flash)
            (directory / "build-provenance.txt").write_text(
                "git_commit=108e96d\ngit_status=\nidf_version=ESP-IDF v5.5.2\nidf_commit=idf-commit\n")

    @staticmethod
    def write(directory, name, value):
        (directory / name).write_text(json.dumps(value))

    def run_package(self):
        return package.package(self.full, self.minimal, "v1.2.0-preview.1", self.output)

    def test_round_trip_and_download_checksums(self):
        records = self.run_package()
        self.assertEqual([record["profile"] for record in records], ["full", "minimal"])
        for directory, record in zip((self.full, self.minimal), records):
            prefix = f"note4-v1.2.0-preview.1-{record['profile']}"
            with zipfile.ZipFile(self.output / f"{prefix}.zip") as archive:
                arguments = shlex.split(archive.read("flash_args").decode())
                self.assertEqual(arguments[:4], ["--flash_mode", "dio", "--flash_size", "16MB"])
                for address, name in zip(arguments[4::2], arguments[5::2]):
                    self.assertEqual(record["flash_files"][address], name)
                    self.assertEqual(archive.read(name), (directory / name).read_bytes())
                self.assertNotIn("books.bin", archive.namelist())
                self.assertIn("licenses/TRMNL_FONT_LICENSE.txt", archive.namelist())
            self.assertEqual((self.output / f"{prefix}-app.bin").read_bytes(),
                             (directory / "application.bin").read_bytes())
        for line in (self.output / "SHA256SUMS").read_text().splitlines():
            expected, name = line.split("  ")
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), expected)
        with self.assertRaises(FileExistsError):
            self.run_package()

    def test_reject_user_data_and_no_partial_publication(self):
        flash = json.loads((self.minimal / "flasher_args.json").read_text())
        flash["flash_files"]["0x912000"] = "books.bin"
        self.write(self.minimal, "flasher_args.json", flash)
        with self.assertRaisesRegex(ValueError, "only bootloader"):
            self.run_package()
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".firmware-package-*")), [])

    def test_reject_shifted_segment_and_escaped_path(self):
        flash = json.loads((self.full / "flasher_args.json").read_text())
        flash["flash_files"]["0x9000"] = flash["flash_files"].pop("0x0")
        flash["bootloader"]["offset"] = "0x9000"
        self.write(self.full, "flasher_args.json", flash)
        with self.assertRaisesRegex(ValueError, "overlaps user/system data"):
            self.run_package()
        with self.assertRaisesRegex(ValueError, "outside"):
            package.build_file(self.full, "../LICENSE")
        (self.full / "external.bin").symlink_to(self.root / "LICENSE")
        with self.assertRaisesRegex(ValueError, "outside"):
            package.build_file(self.full, "external.bin")

    def test_reject_mixed_revisions_missing_images_and_unsafe_labels(self):
        provenance = self.minimal / "build-provenance.txt"
        original = provenance.read_text()
        provenance.write_text(original.replace("108e96d", "different"))
        with self.assertRaisesRegex(ValueError, "different source_commit"):
            self.run_package()
        self.assertFalse(self.output.exists())
        provenance.write_text(original)
        (self.minimal / "application.bin").unlink()
        with self.assertRaisesRegex(ValueError, "Missing artifact"):
            self.run_package()
        with self.assertRaisesRegex(ValueError, "filename-safe"):
            package.package(self.full, self.minimal, "../unsafe", self.output)
        self.assertFalse(self.output.exists())

    def test_matrix_collection_and_separate_library_initialization(self):
        reader = self.root / "reader"
        shutil.copytree(self.full, reader)
        (self.full / "books.bin").write_bytes(b"\xff" * 0x400000)
        self.write(self.full, "config/sdkconfig.json", {
            "COMPILER_OPTIMIZATION_SIZE": True, "SPIFFS_OBJ_NAME_LEN": 96,
            "NOTE4_ENABLE_READER": True, "NOTE4_ENABLE_WIFI": True,
        })
        self.write(reader, "config/sdkconfig.json", {
            "COMPILER_OPTIMIZATION_SIZE": True, "NOTE4_ENABLE_READER": True,
            "NOTE4_ENABLE_USB_HOST": True, "NOTE4_ENABLE_WIFI": False,
        })
        extras = self.root / "extras"
        extras.mkdir()
        (extras / "handbook.html").write_text("<h1>Note4</h1>")
        version = "v1.2.0"
        matrix = []
        for profile, directory in (("full", self.full), ("minimal", self.minimal), ("reader", reader)):
            output = self.root / f"download-{profile}"
            package.package_profiles({profile: directory}, version, output,
                                     library=profile == "full", extras=extras if profile == "full" else None)
            matrix.append(output)
        records = package.collect_packages(matrix, version, self.output)
        self.assertEqual([record["profile"] for record in records], ["full", "minimal", "reader"])
        self.assertEqual(records[2]["enabled_modules"], ["READER", "USB_HOST"])
        self.assertTrue((self.output / "handbook.html").exists())
        for record in records:
            with zipfile.ZipFile(self.output / f"note4-{version}-{record['profile']}.zip") as archive:
                self.assertNotIn("books.bin", archive.namelist())
        with zipfile.ZipFile(self.output / f"note4-{version}-library-init.zip") as archive:
            self.assertEqual(shlex.split(archive.read("flash_args").decode()), ["0x912000", "books.bin"])
            self.assertEqual(archive.read("books.bin"), (self.full / "books.bin").read_bytes())
            self.assertEqual(json.loads(archive.read("manifest.json"))["spiffs_object_name_length"], 96)
            self.assertIn("replaces the entire books partition", archive.read("README.txt").decode())
        for line in (self.output / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(digest, package.checksum(self.output / name))

    def test_failed_matrix_collection_and_library_do_not_publish(self):
        matrix = self.root / "matrix"
        package.package_profiles({"full": self.full}, "v1.2.0", matrix)
        with self.assertRaisesRegex(ValueError, "duplicate matrix download"):
            package.collect_packages([matrix, matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())
        with self.assertRaisesRegex(ValueError, "version does not match"):
            package.collect_packages([matrix], "v1.2.1", self.output)
        self.assertFalse(self.output.exists())
        (matrix / "note4-v1.2.0-full-app.bin").unlink()
        with self.assertRaisesRegex(ValueError, "Missing artifact"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())
        (self.full / "books.bin").write_bytes(b"incomplete")
        with self.assertRaisesRegex(ValueError, "partition size"):
            package.package_profiles({"full": self.full}, "v1.2.0", self.output, library=True)
        self.assertFalse(self.output.exists())

    def test_matrix_preserves_integrity_across_artifact_downloads(self):
        matrix = self.root / "matrix"
        package.package_profiles({"full": self.full}, "v1.2.0", matrix)
        image = matrix / "note4-v1.2.0-full-app.bin"
        original = image.read_bytes()
        image.write_bytes(original[:-1])
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())
        image.write_bytes(original)
        (matrix / "unexpected.bin").write_bytes(b"stale download")
        with self.assertRaisesRegex(ValueError, "complete download set"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())
        (matrix / "unexpected.bin").unlink()
        image.unlink()
        image.symlink_to(self.full / "application.bin")
        with self.assertRaisesRegex(ValueError, "outside the build directory"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())

    def test_matrix_rejects_escaped_or_duplicate_checksum_names(self):
        matrix = self.root / "matrix"
        package.package_profiles({"full": self.full}, "v1.2.0", matrix)
        sums = matrix / "SHA256SUMS"
        original = sums.read_text()
        sums.write_text(original + original.splitlines()[0] + "\n")
        with self.assertRaisesRegex(ValueError, "Duplicate download checksum"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        sums.write_text("0" * 64 + "  ../escaped.bin\n")
        with self.assertRaisesRegex(ValueError, "Invalid download checksum entry"):
            package.collect_packages([matrix], "v1.2.0", self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
