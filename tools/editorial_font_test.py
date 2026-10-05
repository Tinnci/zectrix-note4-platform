#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2", "freetype-py==2.5.1", "scikit-image>=0.25,<0.27"]
# ///
"""Offline optimizer/packing tests; no panel-quality gate."""
import importlib.util
import json
import sys
from pathlib import Path
import numpy as np
from cjk_font_optimizer import Reference, select

spec = importlib.util.spec_from_file_location("editorial", Path(__file__).with_name("generate-editorial-font.py"))
font = importlib.util.module_from_spec(spec)
spec.loader.exec_module(font)
for width in range(1, 35):
    for top in (0, 1, 6):
        mask = np.zeros((28, width), dtype=bool)
        mask[top:20, ::2] = True
        packed, offset, rows = font.pack(mask)
        decoded = np.zeros_like(mask)
        decoded[offset:offset + rows] = np.unpackbits(np.frombuffer(packed, dtype=np.uint8))[:rows * width].reshape(rows, width)
        assert np.array_equal(decoded, mask)
        assert len(packed) <= (mask.size + 7) // 8
assert font.pack(np.zeros((16, 8), dtype=bool)) == (b"", 0, 0)
ring = np.ones((5, 5), dtype=bool)
ring[1:4, 1:4] = False
assert font.topology(ring) == (1, 1)
rows = [{"bytes": 10, "distortion": 2}, {"bytes": 10, "distortion": 1},
        {"bytes": 8, "distortion": 3}, {"bytes": 12, "distortion": 4}]
assert font.pareto(rows) == [rows[1], rows[2]]
# Continuous coverage must expose a horizontal stroke erased by thresholding.
coverage = np.zeros((12, 12))
coverage[2:10, 2] = 1.
coverage[2, 2:10] = .8
coverage[8:10, 4:10] = .45
old = coverage >= 128/255
restored = old.copy()
restored[9, 4:10] = True
model = Reference(coverage)
before = model.candidate("erased", old)
after = model.candidate("restored", restored)
assert after.metrics["core_drift"] < before.metrics["core_drift"]
assert after.metrics["mass_error"] < before.metrics["mass_error"]
assert after.metrics["blur_error"] < before.metrics["blur_error"]
chosen, previous, _ = select(model, old, restored, [("restored", restored)])
assert chosen.name == "restored"
assert all(chosen.metrics[k] <= previous.metrics[k] + 1e-12 for k in previous.metrics)
# Preserve the original native topology constraint even if ink mass is closer.
chosen, _, _ = select(model, old, old, [("restored", restored)])
assert chosen.name == "previous"
extra = old.copy()
extra[10, 10] = True
assert model.candidate("extra", extra).metrics["strong_extra"] == 1
chosen, _, _ = select(model, old, old, [("extra", extra), ("duplicate", old.copy())])
assert chosen.name == "previous"
blank = np.zeros((4, 4), bool)
chosen, _, eligible = select(Reference(blank), blank, blank, [("blank", blank)])
assert not chosen.mask.any() and eligible == 1
if len(sys.argv) > 1:
    report = json.loads(Path(sys.argv[1]).read_text())
    assert len(report["faces"]) == 7
    for face in report["faces"]:
        chosen = face["selected"]
        native = next(r for r in face["candidates"] if r["scale"] == 1 and r["threshold"] == 128)
        assert chosen["method"] == ("pillow-4x" if face["name"] == "Micro" else "coverage-balanced")
        assert chosen["scale"] == (4 if face["name"] == "Micro" else None)
        assert chosen["threshold"] == (128 if face["name"] == "Micro" else None)
        assert 0 <= chosen["fallbacks"] <= face["glyph_count"]
        assert chosen["bytes"] <= face["original_bitmap_bytes"]
        assert native["fallbacks"] == 0
        assert face["cjk_changed"] == sum(d["changed"] for d in face["cjk_decisions"])
        for d in face["cjk_decisions"]:
            assert d["after_topology_error"] <= d["native_topology_error"]
            assert all((d["after"][key] is None if value is None else
                        d["after"][key] <= value+d.get("tolerances", {}).get(key, 0)+1e-12)
                       for key, value in d["before"].items())
            assert d["eligible"] >= 1
    caption = {d["char"]: d for f in report["faces"] if f["name"] == "Caption" for d in f["cjk_decisions"]}
    # Regression cases: the low-resolution threshold used to erase real strokes.
    for char in "信禁":
        assert caption[char]["after"]["outline_stroke_distance"] < caption[char]["before"]["outline_stroke_distance"]
        assert caption[char]["after"]["core_drift"] < caption[char]["before"]["core_drift"]
    assert caption["信"]["after"]["outline_lost_holes"] < caption["信"]["before"]["outline_lost_holes"]
    # Fractional FreeType phases must not leak into the next glyph/reference.
    import freetype as ft
    face = ft.Face(str(Path(report["source_directory"]) / "NotoSansCJKsc-Regular.otf"))
    face.ui_size = 14
    first = font.render(face, "信", 14, 18, 13, 16, ft.FT_LOAD_NO_HINTING)
    for flags in (ft.FT_LOAD_TARGET_NORMAL, ft.FT_LOAD_TARGET_MONO | ft.FT_LOAD_MONOCHROME):
        face.load_char("信", flags | ft.FT_LOAD_RENDER)
        bitmap = face.glyph.bitmap
        raw = np.ctypeslib.as_array(bitmap._FT_Bitmap.buffer, shape=(bitmap.rows*abs(bitmap.pitch),))
        assert np.array_equal(raw, np.asarray(bitmap.buffer, dtype=np.uint8))
    font.render(face, "信", 14, 18, 13, 8, ft.FT_LOAD_NO_HINTING, phase=(.25, -.25))
    assert np.array_equal(first, font.render(face, "信", 14, 18, 13, 16, ft.FT_LOAD_NO_HINTING))
    try:
        font.render(face, "信", 14, 18, 13, 8, ft.FT_LOAD_NO_HINTING, phase=(-5, 0), reject_clipped=True)
        raise AssertionError("Clipped subpixel candidate accepted")
    except ValueError:
        pass
print("PASS: lossless packing, continuous thin-stroke detection, non-regressing selection and native topology protection.")
