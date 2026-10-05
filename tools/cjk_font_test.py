#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2", "freetype-py==2.5.1", "scikit-image>=0.25,<0.27", "gudhi>=3.10,<4", "phasepack==1.5"]
# ///
"""Ordinary synthetic tests for offline diagnostics, not a font-quality gate."""
import importlib.util
import json
import math
import sys
import tempfile
from pathlib import Path
import numpy as np

spec = importlib.util.spec_from_file_location("cjk", Path(__file__).with_name("evaluate-cjk-font.py"))
cjk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cjk)
empty = np.zeros((16, 16), dtype=bool)
assert all(v == 0 for v in cjk.diagnostics(empty, empty.astype(float)).values())
ring = empty.copy()
ring[3:13, 3:13] = True
ring[5:11, 5:11] = False
assert all(v == 0 for v in cjk.diagnostics(ring, ring.astype(float)).values())
filled = ring.copy()
filled[5:11, 5:11] = True
metrics = cjk.diagnostics(filled, ring.astype(float))
assert metrics["white_channel_loss_px"] > 0
assert metrics["topology_count_error"] == 1
assert metrics["area_relative_error"] > 0
assert metrics["lost_holes"] == 1
assert metrics["extra_holes"] == 0
assert cjk.diagnostics(ring, filled.astype(float))["extra_holes"] == 1
wider_gap = ring.copy()
wider_gap[4:12, 4:12] = False
assert cjk.diagnostics(wider_gap, ring.astype(float))["white_channel_expansion_px"] > 0
shifted = np.roll(ring, 1, axis=1)
metrics = cjk.diagnostics(shifted, ring.astype(float))
assert metrics["topology_count_error"] == 0
assert metrics["blur_mse"] > 0  # counts alone miss displacement
assert metrics["centerline_loss"] > 0
assert metrics["missing_centerline_fraction"] > 0
assert cjk.diagnostics(np.roll(ring, 2, axis=1), ring.astype(float))["spurious_centerline_fraction"] > 0
assert cjk.diagnostics(np.roll(ring, 1, axis=0), ring.astype(float))["ink_bottom_error_px"] == 1
assert cjk.morphology(ring)["ink_height"] == 10
assert cjk.morphology(ring)["left_bearing"] == 3
assert set(chr(cp) for cp in range(32, 127)).issubset(cjk.catalog_chars())
assert set("主页").issubset(cjk.catalog_chars())
assert cjk.diagnostics(ring, ring.astype(float), cjk.morphology(ring), cjk.morphology(ring)) == cjk.diagnostics(ring, ring.astype(float))
with tempfile.TemporaryDirectory(prefix="note4-font-test-") as directory:
    for width, height in [(300, 400), (400, 300)]:
        output = Path(directory) / f"test-{width}.png"
        cjk.test_page({"0": ring}, "Calibration", output, width, height)
        header = f"P4\n{width} {height}\n".encode()
        pbm = output.with_suffix(".pbm").read_bytes()
        assert pbm.startswith(header)
        data = np.frombuffer(pbm[len(header):], dtype=np.uint8).reshape(height, (width+7)//8)
        decoded = np.unpackbits(data, axis=1)[:, :width].astype(bool)
        with cjk.Image.open(output) as image:
            assert image.size == (width, height)
            assert np.array_equal(decoded, ~np.asarray(image, dtype=bool))
assert cjk.pair_distance(ring, ring, .65) == 0
assert cjk.pair_distance(ring, shifted, .65) > 0
assert cjk.pair_distance(ring, shifted, 1.) < cjk.pair_distance(ring, shifted, 0.)
def candidate(name, size, error):
    return {"name": name, "bitmap_bytes": size, "metrics": {
        metric: {"mean": error, "p95": error} for metric in
        ("blur_mse", "white_channel_loss_px", "topology_count_error", "centerline_loss")}}
assert cjk.frontier([candidate("small", 8, 2), candidate("clear", 10, 1),
                     candidate("dominated", 12, 3)]) == ["small", "clear"]
if len(sys.argv) > 1:
    report = json.loads(Path(sys.argv[1]).read_text())
    assert 0 < len(report["faces"]) <= len(cjk.FACES)
    for face in report["faces"]:
        assert 1 <= len(face["candidates"]) <= 7
        if "methods" in report:
            assert {c["name"] for c in face["candidates"]} == set(report["methods"])
        assert face["diagnostic_frontier"] == cjk.frontier(face["candidates"])
        for candidate in face["candidates"]:
            assert candidate["bitmap_bytes"] > 0
            expected_pairs = sum(a in candidate["glyphs"] and b in candidate["glyphs"] for a, b in cjk.PAIRS)*3
            assert len(candidate["pairs"]) == expected_pairs
            expected_groups = {"ui_ascii"} if face["role"] == "Micro" else set(cjk.GROUPS) | {"ui_cjk"}
            if any(ord(c) < 127 for c in candidate["glyphs"]):
                expected_groups.add("ui_ascii")
            if candidate["name"] == "embedded":
                expected_groups = {group for group, members in {**cjk.GROUPS,
                    "ui_ascii": "".join(c for c in candidate["glyphs"] if c in cjk.catalog_chars() and ord(c) < 127),
                    "ui_cjk": "".join(c for c in candidate["glyphs"] if c in cjk.catalog_chars() and 0x3400 <= ord(c) <= 0x9fff)}.items()
                    if any(c in candidate["glyphs"] for c in members)}
            assert set(candidate["groups"]) == expected_groups
            assert candidate["catalog_with_index_bytes"] >= candidate["catalog_bitmap_bytes"]
            for metric in candidate["metrics"].values():
                assert all(math.isfinite(v) and v >= 0 for v in metric.values())
                assert metric["max"] >= metric["p95"]
    assert len(report["panel_trials"]) == sum(len(f["candidates"])*2 for f in report["faces"])
    assert len(set(t["id"] for t in report["panel_trials"])) == len(report["panel_trials"])
    assert set(report["example_trial_order"]) == {t["id"] for t in report["panel_trials"]}
    with tempfile.TemporaryDirectory(prefix="note4-report-test-") as directory:
        summary = Path(directory) / "summary.md"
        cjk.write_summary(report, summary)
        assert "Missing centerline P95" in summary.read_text()
print("PASS: empty/exact masks, lost white channels, displaced strokes and pair blur sensitivity.")
