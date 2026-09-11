"""Exercise the font generator and produce an independent BDF pixel fixture."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

SCRIPT = Path(__file__).with_name("generate-reader-font.py")
SPEC = importlib.util.spec_from_file_location("reader_font_generator", SCRIPT)
font = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(font)


class FontPackingTest(unittest.TestCase):
    def test_shared_tiles_and_widths(self):
        narrow = bytes([8]) + bytes([0xAA, 0]) * 16
        wide = bytes([16]) + bytes([0xAA, 0]) * 16
        packed = font.pack_glyphs(narrow + wide)
        self.assertEqual(packed[0], 2)
        self.assertEqual(packed[1:9], packed[9:17])
        self.assertEqual(packed[17:], bytes([0xAA]) * 8 + bytes(8))

    def test_rejects_invalid_records(self):
        for raw in (b"", bytes(32), bytes([9]) + bytes(32)):
            with self.assertRaises(ValueError):
                font.pack_glyphs(raw)

    def test_width_bit_boundary(self):
        narrow = bytes([8]) + bytes(32)
        wide = bytes([16]) + bytes(32)
        self.assertEqual(font.pack_glyphs(narrow * 8 + wide)[:2], bytes([0, 1]))


def fixture(directory):
    # Different quadrants and advances expose tile order, endianness and width
    # mistakes. Missing slots deliberately exercise the replacement glyph.
    glyphs = {
        0x20: (8, [0] * 16),
        0x41: (8, [0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0] * 2),
        0x4E2D: (16, [0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180] * 2),
        0xFFFD: (16, [0x1234 + row * 0x101 for row in range(16)]),
    }
    source = directory / "source.bdf"
    blocks = []
    for codepoint, (width, rows) in glyphs.items():
        blocks.append(f"ENCODING {codepoint}\nDWIDTH {width} 0\nBITMAP\n" +
                      "".join(f"{row:0{width // 4}X}\n" for row in rows) + "ENDCHAR\n")
    source.write_text("".join(blocks))
    raw_path = directory / "expected.raw"
    subprocess.run([sys.executable, str(SCRIPT), str(source), str(directory / "fixture.bin"),
                    "--raw-output", str(raw_path)], check=True)
    # Build the reference directly from the BDF fixture, independently of packing.
    raw = bytearray()
    for first, last in font.RANGES:
        for codepoint in range(first, last + 1):
            width, rows = glyphs.get(codepoint, glyphs[0xFFFD])
            raw.append(width)
            for row in rows:
                raw.extend((row << (16 - width)).to_bytes(2, "big"))
    if raw_path.read_bytes() != raw:
        raise AssertionError("BDF conversion changed fixture pixels")


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False)
    if not result.result.wasSuccessful():
        sys.exit(1)
    fixture(Path(sys.argv[1]))
