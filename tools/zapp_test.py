"""Exercise CLI output against both the Python reader and the device streaming parser."""

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import zapp

REPO = Path(__file__).resolve().parent.parent
BINARY = sys.argv.pop(1)


class Packages(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="note4-zapp-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(REPO / "tools/zapp.py"), *map(str, args)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0 if success else 1, result.stderr)
        return json.loads(result.stdout) if success else result.stderr

    def test_build_pack_inspect_and_rebuild(self):
        for name, side, quota in (("Calculator", 16, 10000), ("Flashcards", 32, 5000)):
            output = self.root / "with spaces" / f"{name}.zapp"
            meta = self.cli("build", REPO / f"apps/{name}.app.json", "-o", output)
            self.assertEqual((meta["name"], meta["icon_size"], meta["instruction_quota"]), (name, side, quota))
            first = output.read_bytes()
            self.cli("pack", REPO / f"apps/{name}.lua", "--icon", REPO / f"apps/{name}.pbm",
                     "--name", name, "--version", "1.0.0", "--author", "Note4", "--permission", "display",
                     "--permission", "input", "--quota", quota, "-o", output)
            self.assertEqual(output.read_bytes(), first)
            self.assertEqual(self.cli("inspect", output)["package_bytes"], len(first))
            self.assertFalse(list(output.parent.glob(".zapp-*")))

    def test_manifest_failures_preserve_output(self):
        manifest = json.loads((REPO / "apps/Calculator.app.json").read_text())
        manifest.update(entry=str(REPO / "apps/Calculator.lua"), icon=str(REPO / "apps/Calculator.pbm"))
        path, output = self.root / "app.json", self.root / "existing.zapp"
        output.write_bytes(b"keep")
        for change in ({"instruction_quota": 10001}, {"instruction_quota": True}, {"guest_api": 2},
                       {"format_version": 0}, {"permissions": ["network"]}, {"permissions": ["display", "display"]},
                       {"name": "x" * 64}, {"name": "bad\nname"}, {"version": "1.0 beta"},
                       {"author": ""}, {"entry": None}, {"unknown": 1}):
            path.write_text(json.dumps(manifest | change))
            self.cli("build", self.root, "-o", output, success=False)
            self.assertEqual(output.read_bytes(), b"keep")
        path.write_text(json.dumps(manifest))
        self.cli("build", self.root, "-o", output)
        self.assertNotEqual(output.read_bytes(), b"keep")
        path.write_text("[]")
        self.cli("build", path, success=False)
        path.write_text("{")
        self.cli("build", path, success=False)

    def test_icons_and_source_bounds(self):
        path = self.root / "icon.pbm"
        pixels = b"# \n\r" + bytes(range(28))
        for separator in (b"\n", b"\r\n"):
            path.write_bytes(b"P4\n# original icon\n16 16" + separator + pixels)
            self.assertEqual(zapp.read_icon(path), (16, pixels))
        for data in (b"P4\n16 16\n" + pixels + b"x", b"P4\n16 16\n" + pixels[:-1],
                     b"P1\n16 32\n", b"P1\n16 16\n" + b"0 " * 255, b"P1\n16 16\n" + b"0 " * 257):
            path.write_bytes(data)
            with self.assertRaises(zapp.PackageError):
                zapp.read_icon(path)
        for source in (b"", b" " * 32769, b"\x1bLua", b"-- \xff", b"--\0", b"--\x7f", b"--\xed\xa0\x80"):
            with self.assertRaises(zapp.PackageError):
                zapp.source_text(source)

    def test_device_parser_and_installation(self):
        fixtures = self.root / "fixtures"
        fixtures.mkdir()
        original, _ = zapp.build_manifest(REPO / "apps/Calculator.app.json")
        cases = {}
        cases["valid-calculator"] = original
        cases["valid-flashcards"], _ = zapp.build_manifest(REPO / "apps/Flashcards.app.json")
        for side in (16, 32):
            text = "-- 中文\n".encode() + b" " * (32768 - len("-- 中文\n".encode()))
            cases[f"valid-max-{side}"] = zapp.make_package(text, (side, bytes(side * side // 8)), {
                "name": "便携应用", "version": "1.2.3-preview+4", "author": "开发者", "permissions": [], "instruction_quota": 100})
        def changed(offset, data):
            return original[:offset] + data + original[offset + len(data):]
        for offset, data in ((0, b"NOPE"), (4, b"\x02\0"), (6, b"\x02\0"), (8, b"\x02"),
                             (9, b"\0"), (9, b"\x18"), (10, b"\x04\0"), (11, b"\x80"),
                             (12, struct.pack("<I", 99)), (12, struct.pack("<I", 10001)),
                             (12, struct.pack("<I", 0xffffffff)), (16, struct.pack("<I", 0)),
                             (16, struct.pack("<I", 32769)), (20, b"\x01"), (24, b"\0" * 64),
                             (24, b"a" * 64), (24, b"bad\n"), (24, b"\xc0\xaf"), (40, b"x"),
                             (88, b" "), (88, b"\xc2\xa0"), (112, b"\0" * 48), (120, b"\xff"),
                             (192, b"\x1bLua"), (192, b"\0"), (192, b"\xf4\x90\x80\x80")):
            cases[f"bad-field-{len(cases)}"] = changed(offset, data)
        for length in (0, 1, 4, 23, 24, 87, 88, 159, 160, 161, 191, 192, len(original) - 1):
            cases[f"bad-truncated-{length}"] = original[:length]
        cases["bad-trailing"] = original + b"x"
        cases["bad-incomplete-utf8"] = original[:-1] + b"\xc2"
        cases["bad-over-limit"] = b" " * (zapp.PACKAGE_LIMIT + 1)
        for name, data in cases.items():
            (fixtures / f"{name}.zapp").write_bytes(data)
            if name.startswith("valid-"):
                zapp.inspect_package(data)
            else:
                with self.assertRaises(zapp.PackageError, msg=name):
                    zapp.inspect_package(data)
        subprocess.run([BINARY, str(fixtures), str(self.root / "store")], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
