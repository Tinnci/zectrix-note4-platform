#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=11,<13", "numpy>=2,<3", "scipy>=1.14,<2"]
# ///
"""Algorithm and cross-language fixtures; no screenshot baseline or quality gate."""
import argparse
import importlib.util
import json
import random
import re
import struct
import unittest
import tempfile
from pathlib import Path
import numpy as np
from PIL import Image
from digit_font import ROOT, bits_of, decode, encode_codec, source_glyphs, rasterize, trim, tile_pack, tile_unpack, pack

def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

analysis = load_tool("optimize-digit-font")
generator = load_tool("generate-large-digits")

class CodecTests(unittest.TestCase):
    def test_recipe_input_validation(self):
        valid = json.loads((ROOT / "docs/design/date-digits/raster-settings.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recipe.json"
            for mutation in ("height", "duplicate", "threshold", "method", "style"):
                recipe = json.loads(json.dumps(valid))
                if mutation == "height": recipe["height"] = 32
                elif mutation == "duplicate": recipe["glyphs"][1] = recipe["glyphs"][0]
                elif mutation == "threshold": recipe["glyphs"][0]["threshold"] = True
                elif mutation == "method": recipe["glyphs"][0]["method"] = "unknown"
                elif mutation == "style": recipe["glyphs"][0]["style"] = -1
                path.write_text(json.dumps(recipe))
                with self.assertRaises(ValueError):
                    generator.load_recipe(path)
            with self.assertRaises(FileNotFoundError):
                generator.load_recipe(Path(directory) / "missing.json")
    def test_roundtrip(self):
        randomizer = random.Random(1729)
        for width in (1, 7, 8, 9, 16, 31, 42):
            for height in (1, 7, 48, 128):
                count = width * height
                for bits in ([0] * count, [1] * count, [i % 2 for i in range(count)],
                             [randomizer.randrange(2) for _ in range(count)]):
                    for codec in range(4):
                        self.assertEqual(bits, decode(encode_codec(bits, width, codec), width, height, codec))

    def test_malformed(self):
        for payload, width, height, codec in ((b"", 1, 1, 0), (b"\0", 0, 1, 0),
                                              (b"\0", 1, 0, 0), (b"\0", 1, 1, 4),
                                              (b"\x7f", 1, 1, 1), (b"\0\0", 1, 1, 2)):
            with self.assertRaises(ValueError):
                decode(payload, width, height, codec)
        with self.assertRaises(ValueError):
            encode_codec([1, 2], 1, 1)

    def test_tiles(self):
        glyphs = [Image.new("1", (width, 13), 1) for width in (1, 7, 17)]
        for image in glyphs:
            image.putpixel((image.width - 1, 12), 0)
            image.putpixel((0, 0), 0)
        for size in (4, 8):
            dictionary, indices, shapes = tile_pack(glyphs, size)
            restored = tile_unpack(dictionary, indices, shapes, size)
            self.assertEqual([bits_of(g) for g in glyphs], [bits_of(g) for g in restored])

    def test_native_header_matches_artwork_and_recipe(self):
        parameters = generator.load_recipe(ROOT / "docs/design/date-digits/raster-settings.json")
        text = (ROOT / "components/ui/font/large_digits.h").read_text()
        metadata = [tuple(map(int, entry)) for entry in re.findall(r"\{(\d+), (\d+), (\d+), (\d+)\}", text)]
        data = bytes(int(byte, 16) for byte in re.findall(r"0x([0-9a-f]{2})", text))
        self.assertEqual(len(metadata), 50)
        for source, (method, threshold), (offset, size, width, codec) in zip(source_glyphs(), parameters, metadata):
            glyph = trim(rasterize(source, 48, threshold, method))
            self.assertEqual(glyph.width, width)
            self.assertLessEqual(offset + size, len(data))
            self.assertEqual(bits_of(glyph), decode(data[offset:offset + size], width, 48, codec))

class QualityTests(unittest.TestCase):
    def test_topology_uses_dual_connectivity(self):
        ring = np.ones((9, 9), bool); ring[2:7, 2:7] = False
        self.assertEqual(analysis.topology(ring), (1, 1))
        ring[0:3, 4] = False
        self.assertEqual(analysis.topology(ring), (1, 0))
        self.assertEqual(analysis.topology(np.eye(3, dtype=bool)), (1, 0))

    def test_simulation_rotation_and_inverse(self):
        randomizer = np.random.default_rng(2)
        ink, history = randomizer.random((20, 16)), randomizer.random((20, 16))
        for scenario in analysis.SCENARIOS:
            expected = analysis.simulate(ink, scenario, history)
            inverse = analysis.simulate(ink, scenario, history, True)
            np.testing.assert_allclose(expected + inverse, 1, atol=1e-12)
            for rotation in range(4):
                np.testing.assert_allclose(analysis.simulate(ink, scenario, history, rotation=rotation), np.rot90(expected, rotation))

    def test_ssim_and_sharpness(self):
        image = np.zeros((20, 20)); image[5:15, 5:15] = 1
        region = np.ones_like(image, bool)
        self.assertAlmostEqual(analysis.ssim(image, image, region), 1)
        self.assertLess(analysis.ssim(image, np.roll(image, 2, axis=1), region), 1)
        sharpness = analysis.sharpness_report()
        self.assertLess(sharpness[0]["edge_10_90_width_px"], sharpness[-1]["edge_10_90_width_px"])

    def test_pareto_filters_topology_and_dominance(self):
        def item(size, error, topology=0):
            return {"bytes": size, "quality": {"distortion": error, "topology_error": topology}}
        a, b, c, unsafe = item(10, 1), item(8, 2), item(11, 2), item(1, 0, 1)
        self.assertEqual(analysis.pareto([a, b, c, unsafe]), [a, b])

    def test_contour_degradation_is_detected(self):
        source = Image.new("L", (64, 96), 255)
        from PIL import ImageDraw
        ImageDraw.Draw(source).ellipse((4, 4, 59, 91), fill=0)
        reference = analysis.reference_for(source, 48)
        normal = rasterize(source)
        damaged = normal.copy()
        ImageDraw.Draw(damaged).rectangle((0, 0, damaged.width // 2, 47), fill=255)
        self.assertLess(analysis.evaluate(normal, reference)["distortion"], analysis.evaluate(damaged, reference)["distortion"])

def write_fixtures(path):
    randomizer = random.Random(1729)
    records = []
    for width in (1, 7, 8, 9, 16, 31, 42):
        for height in (1, 7, 48, 128):
            count = width * height
            patterns = [[0] * count, [1] * count, [i % 2 for i in range(count)],
                        [randomizer.randrange(2) for _ in range(count)]]
            for bits in patterns:
                for codec in range(4):
                    records.append((width, height, codec, encode_codec(bits, width, codec), pack(bits)))
    parameters = generator.load_recipe(ROOT / "docs/design/date-digits/raster-settings.json")
    for source, (method, threshold) in zip(source_glyphs(), parameters):
        glyph = trim(rasterize(source, 48, threshold, method)); bits = bits_of(glyph)
        for codec in range(4):
            records.append((glyph.width, 48, codec, encode_codec(bits, glyph.width, codec), pack(bits)))
    path.write_bytes(b"".join(struct.pack("<HHBH", w, h, c, len(payload)) + payload + raw
                              for w, h, c, payload, raw in records))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", type=Path)
    args = parser.parse_args()
    if args.fixtures:
        write_fixtures(args.fixtures)
    unittest.main(argv=["digit_font_test"], verbosity=2)
