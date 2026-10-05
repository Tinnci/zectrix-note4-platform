#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2", "scikit-image>=0.25,<0.27", "gudhi>=3.10,<4", "phasepack==1.5"]
# ///
"""Synthetic, independently known cases for every quantitative metric family."""
import json
import math
from pathlib import Path
import tempfile
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as ndi
from skimage.morphology import skeletonize
from cjk_metrics import Features, compare, confusion, robustness, directional_widths, stats
from cjk_context import corpus, pair_graph, load_embedded, source_mask, aggregate, page_context, contextual_neighbors
from cjk_measurements import compression, energy, human, pse, measurements, optical

ROOT = Path(__file__).resolve().parent.parent


def rejected(fn):
    try:
        fn()
    except ValueError:
        return
    raise AssertionError("Invalid input was accepted")


assert stats([])["mean"] is None
assert stats([1, 2, 100], [100, 100, 0])["p95"] == 2
assert stats([np.nan, 5])["count"] == 1
assert stats([-10, 0, 10])["abs_p95"] == 10
rejected(lambda: stats([1, 2], [1, -1]))
rejected(lambda: stats([1, 2], [1]))
rejected(lambda: stats([np.nan, 2], [np.inf, 1]))
empty = np.zeros((24, 24), bool)
bar = empty.copy()
bar[10:13, 3:21] = True
for thickness in (1, 2, 3, 4):
    sample = empty.copy()
    sample[8:8+thickness, 3:21] = True
    width = directional_widths(sample, skeletonize(sample))["horizontal"]
    assert width["count"] > 0
    assert abs(width["mean"]-thickness) <= .125, (thickness, width)
    assert abs(directional_widths(sample.T, skeletonize(sample.T))["vertical"]["mean"]-thickness) <= .125
diagonal = np.eye(24, dtype=bool)
assert abs(directional_widths(diagonal, skeletonize(diagonal))["diagonal_down"]["mean"]-math.sqrt(2)) <= .125
assert abs(directional_widths(np.fliplr(diagonal), skeletonize(np.fliplr(diagonal)))["diagonal_up"]["mean"]-math.sqrt(2)) <= .125
f = Features(bar)
exact = compare(f, f)
assert exact["image"]["mse"] == 0
assert exact["image"]["psnr_db"] is None and exact["image"]["psnr_exact"]
assert exact["image"]["ssim"] == 1 and abs(exact["image"]["fsim_phasepack"]-1) < 1e-12
assert exact["image"]["adaptive_ms_ssim"] == 1
assert exact["centroid_drift_px"] == 0
assert all(v == 0 for v in exact["persistence_bottleneck"].values())
thick_bar = bar.copy()
thick_bar[13, 3:21] = True
thickening = compare(Features(thick_bar), f)
assert thickening["weight_error"]["normalized_effective_width"] > 0
assert thickening["directional_width_error_px"]["horizontal"] > 0
assert abs(thickening["centroid_drift_px"]-.5) < 1e-9
shifted = compare(Features(ndi.shift(bar, (0, 1), order=0, mode="constant")), f)
assert abs(shifted["centroid_drift_px"]-1) < 1e-9
assert shifted["image"]["boundary_distance_px"]["max"] == 1
assert shifted["image"]["ssim"] < 1 and shifted["image"]["signed_distance_mae_px"] > 0
assert compare(Features(empty), f)["image"]["boundary_distance_px"]["max"] is None
assert Features(empty).descriptors()["normalized_effective_width"] is None
ring = empty.copy()
ring[4:20, 4:20] = True
ring[7:17, 7:17] = False
assert len(Features(ring).persistence["1"]["diagram"]) == 1
broken = bar.copy()
broken[:, 12] = False
assert compare(Features(broken), f)["component_splits"] == 1
assert compare(f, Features(broken))["component_merges"] == 1
assert compare(Features(broken), f)["largest_missing_patch_px"] > 0
neighbors = confusion({"一": bar, "二": bar.copy(), "三": ring}, {"一": bar.astype(float), "二": bar.astype(float), "三": ring.astype(float)})
assert neighbors["一"]["nearest"][0]["collapsed"]
assert neighbors["三"]["reference_margin"] > 0
assert {r["scenario"] for r in robustness(bar, bar.astype(float))} == {"blur", "contrast", "phase_x", "phase_y", "residue"}
assert all(r["coverage_mse"] >= 0 for r in robustness(bar, bar.astype(float)))
assert aggregate({"a": {"value": None}, "b": {"value": 1}}, {"a": 1, "b": 3})["unweighted"]["value"]["count"] == 1
graph = pair_graph(["甲乙甲", "甲", "乙丙"])
assert graph["frequency"]["甲"] == 3 and graph["adjacency"]["甲乙"] == 1
assert next(p for p in graph["cooccurrence"] if p["pair"] == "乙甲")["documents"] == 1
catalog = corpus(ROOT / "components/note4_app/include/note4_strings.inc")
assert len(catalog["entries"]) > 900
assert contextual_neighbors(neighbors, catalog)[0]["collapsed"]
assert "%04d" not in next(e["text"] for e in catalog["entries"] if e["id"] == "CalendarTodayDate")
embedded = load_embedded(ROOT / "components/ui/font/editorial_font.h")
assert len(embedded) == 7 and "主" in embedded["Caption"]["masks"]
assert all(m.dtype == bool for face in embedded.values() for m in face["masks"].values())
codecs = compression({"一": bar, "二": empty})
assert all(c["lossless"] for c in codecs["codecs"])
assert codecs["row_trim_saved_bytes"] > 0
assert 0 <= codecs["empirical_byte_entropy_bits"] <= 8
assert 0 <= codecs["empirical_next_byte_conditional_entropy_bits"] <= 8
assert energy([{"id": "test", "measurement_boundary": "synthetic", "samples": [
    {"time_s": 0, "voltage_v": 3, "current_a": 2}, {"time_s": 2, "voltage_v": 3, "current_a": 2}]}])["runs"][0]["energy_j"] == 12
rejected(lambda: energy([{"samples": [{"time_s": 1, "voltage_v": 3, "current_a": 2}, {"time_s": 0, "voltage_v": 3, "current_a": 2}]}]))
trials = [{"participant": p, "candidate": "a", "role": "Caption", "expected": c, "answer": c, "time_ms": 500}
          for p in ("p1", "p2") for c in ("甲", "乙")]
h = human(trials)["groups"][0]
assert h["accuracy"] == 1 and h["accuracy_cluster_bootstrap_ci95"] == [1, 1]
assert h["mi_bits_empirical"] == 1
assert h["mean_time_cluster_bootstrap_ci95_ms"] == [500, 500]
assert h["mi_cluster_bootstrap_ci95_bits"] == [1, 1]
assert human(trials[:1])["groups"][0]["accuracy_cluster_bootstrap_ci95"] is None
rejected(lambda: human([{**trials[0], "time_ms": -1}]))
assert all(measurements()[k]["status"] == "needs_measurement" for k in ("energy", "recognition", "optical", "weight_pse", "refresh"))
judgments = [{"participant": "p", "candidate": "a", "role": "Caption", "weight_delta": x, "test_heavier": i < positive}
             for x, positive in ((-1, 2), (0, 5), (1, 8)) for i in range(10)]
assert abs(pse(judgments)["groups"][0]["pse_weight_delta"]) < .01
assert pse([{**r, "weight_delta": r["weight_delta"]*1e-6} for r in judgments])["groups"][0]["status"] == "measured_fit"
assert pse([{**judgments[0], "test_heavier": True}])["groups"][0]["pse_weight_delta"] is None
with tempfile.TemporaryDirectory(prefix="note4-quant-test-") as directory:
    base = Path(directory)
    Image.fromarray(np.where(bar, 0, 255).astype(np.uint8)).save(base / "target.png")
    measured = optical([{"id": "synthetic", "capture": "target.png", "target": "target.png", "previous": "target.png",
                         "black_level": 0, "white_level": 255, "registration": "synthetic identical pixels"}], base)["captures"][0]
    assert measured["coverage_mse"] == 0 and measured["ssim"] == 1
    assert measured["ghost_residue_projection"] is None
    # Known ASCII source rows, no catalog/rendering dependencies.
    e = {"source": "reader", "size": 16, "source_width": 8, "source_rows": [0x8000]*16,
         "width": 8, "height": 16, "cp": ord("I"), "style": 0, "x": 4, "y": 4}
    mask = source_mask(e)
    assert np.array_equal(mask[:, 0], np.ones(16, bool)) and mask.sum() == 16
    dim = source_mask({**e, "style": 4})
    assert dim.sum() < mask.sum()
    frame = np.zeros((300, 400), bool)
    frame[4:20, 4:12] = mask
    Image.fromarray(~frame).save(base / "test.pbm")
    event = {**e, "role": "Reader16", "run": 0, "clip_left": 4, "clip_top": 4, "clip_right": 12, "clip_bottom": 20, "inverted": False}
    (base / "test.text.json").write_text(json.dumps({"width": 400, "height": 300, "language": "en", "dropped": 0, "glyphs": [event]}))
    context = page_context(base, embedded)
    assert context["pages"][0]["glyphs"][0]["expected_ink_retained"] == 1
    assert context["pages"][0]["glyphs"][0]["normalized_effective_width"] is not None
    assert context["frequencies_by_font"][0]["frequency"]["I"] == 1
    assert context["glyph_shapes"][0]["descriptors"]["ink_height_px"] == 16
    assert context["glyph_shapes"][0]["descriptors"]["advance_px"] == 8
    # Repaint and erase do not inflate visible frequency.
    trace_path = base / "test.text.json"
    trace = json.loads(trace_path.read_text())
    trace["glyphs"].append({**event, "run": 9})
    trace_path.write_text(json.dumps(trace))
    assert page_context(base, embedded)["frequencies_by_font"][0]["frequency"]["I"] == 1
    Image.fromarray(np.ones((300, 400), bool)).save(base / "test.pbm")
    erased = page_context(base, embedded)
    assert not erased["frequencies_by_font"]
    assert len(erased["pages"][0]["occluded_glyphs"]) == 1
if len(sys.argv) > 1:
    report = json.loads(Path(sys.argv[1]).read_text())
    for face in report["faces"]:
        common = face["comparison_glyphs"]
        assert common and len(face["candidates"]) == len(report.get("methods", range(7)))
        for candidate in face["candidates"]:
            assert set(candidate["extended_glyphs"]) == set(candidate["glyphs"])
            assert set(common).issubset(candidate["extended_glyphs"])
            assert candidate["extended_summary"]["unweighted"]["errors.image.mse"]["count"] == len(candidate["glyphs"])
            assert candidate["compression"]["codecs"][0]["bitmap_bytes"] == candidate["bitmap_bytes"]
            assert len(candidate["confusion"]) == sum(not c.isspace() for c in candidate["glyphs"])
    json.dumps(report, allow_nan=False)
    if "page_context" in report:
        for page in report["page_context"]["pages"]:
            assert page["trace_dropped"] == 0
            for event in page["glyphs"]:
                assert 0 <= event["clip_left"] < event["clip_right"] <= page["width"]
                assert 0 <= event["clip_top"] < event["clip_bottom"] <= page["height"]
                if event["expected_ink_retained"] is not None:
                    assert 0 < event["expected_ink_retained"] <= 1
        # Source rows and firmware glyphs explain actual native drawing pixels.
        samples = [e for page in report["page_context"]["pages"] for e in page["glyphs"]
                   if e["source"] == "reader" and e["expected_ink_retained"] is not None]
        assert samples and sum(e["expected_ink_retained"] == 1 for e in samples)/len(samples) > .99
print("PASS: calibrated bars, direction/weight/centroid, topology/confusion, corpus/context, codecs and real-data ingestion.")
