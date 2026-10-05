#!/usr/bin/env python3
# /// script
# dependencies = ["numpy>=2,<3", "scipy>=1.14,<2", "scikit-image>=0.25,<0.27"]
# ///
"""Synthetic correctness tests, not a font-quality or release gate."""
import numpy as np
from cjk_balance_metrics import measure, fourier
from cjk_font_optimizer import Reference, select, cell_distance

ring = np.zeros((20, 20), bool)
ring[4:16, 4:16] = True
ring[6:14, 6:14] = False
exact = measure(ring, ring.astype(float))
for key in ("ink_drift", "core_drift", "phase_drift", "orientation_js_bits", "strong_extra", "added_pull", "missing_pull"):
    assert abs(exact[key]) < 1e-12
assert exact["core_tilt_abs_deg"] is None  # no meaningful axis for a symmetric core
shifted = np.roll(ring, 1, axis=1)
metrics = measure(shifted, ring.astype(float))
assert abs(metrics["ink_dx"]-1) < 1e-12
assert abs(metrics["phase_dx"]-1) < 1e-12
assert abs(metrics["phase_dy"]) < 1e-12
assert abs(metrics["high_energy_change_pp"]) < 1e-12  # power alone misses the shift
assert metrics["low_phase_rms_deg"] > 0
assert metrics["strong_extra"] > 0
assert abs(metrics["added_dx"]+metrics["missing_dx"]-metrics["ink_dx"]) < 1e-12
coverage = ring.astype(float)
coverage[10, 6:14] = .4
coverage[11, 6:14] = .4
thin = measure(ring, coverage)
assert thin["missing_coverage_pct"] > 0
assert thin["core_drift"] > 0  # subthreshold strokes must not disappear from measurement
for invalid in (np.full((3, 3), np.nan), np.ones((3, 3))*1.1, np.empty((0, 0)), np.zeros(3)):
    try:
        Reference(invalid)
        raise AssertionError("Invalid reference accepted")
    except ValueError:
        pass
try:
    select(Reference(ring), ring, ring, [("wrong shape", np.zeros((4, 4), bool))])
    raise AssertionError("Changed canvas accepted")
except ValueError:
    pass
try:
    fourier(np.pad(ring, 30), np.pad(ring, 30))
    raise AssertionError("Unsupported Fourier canvas accepted")
except ValueError:
    pass
assert cell_distance(np.array([[0., 0.]]), np.array([[0., 0.]])) == 0
assert cell_distance(np.empty((0, 2)), np.array([[0., 0.]])) is None
assert cell_distance(np.array([[0., 1.]]), np.array([[0., 0.]])) == .5
assert abs(cell_distance(np.array([[1., 1.]]), np.array([[0., 0.]]))-np.sqrt(.5)) < 1e-12
rng = np.random.default_rng(1)
for count in (1, 3, 9, 35, 180):
    cells = np.unique(rng.integers(0, 24, (count, 2)), axis=0)
    points = rng.uniform(-3, 28, (100, 2))
    delta = np.maximum(np.abs(points[:, None, :]-cells[None, :, :])-.5, 0)
    brute = np.sqrt(np.min(np.sum(delta*delta, axis=-1), axis=1)).mean()
    assert abs(cell_distance(points, cells)-brute) < 1e-12
# True subpixel strokes exist even when every downsampled native pixel is <.5.
outline = np.zeros((48, 48))
outline[8:40, 8:12] = 1
outline[30:32, 12:40] = 1
ref = outline.reshape(12, 4, 12, 4).mean(axis=(1, 3))
erased = ref >= 128/255
restored = erased.copy()
restored[7, 3:10] = True
true_model = Reference(ref, outline)
assert true_model.candidate("restored", restored).metrics["outline_stroke_distance"] < true_model.candidate("erased", erased).metrics["outline_stroke_distance"]
# A filled ring loses both a resolvable white core and its actual enclosed hole.
true_model = Reference(ring, np.repeat(np.repeat(ring.astype(float), 4, axis=0), 4, axis=1))
filled = ring.copy()
filled[6:14, 6:14] = True
assert true_model.candidate("ring", ring).metrics["outline_white_distance"] == 0
assert true_model.candidate("filled", filled).metrics["outline_white_distance"] > 0
assert true_model.candidate("filled", filled).metrics["outline_lost_holes"] == 1
assert true_model.candidate("ring", ring).metrics["outline_lost_holes"] == 0
two_holes = np.zeros((16, 16), bool)
two_holes[2:14, 2:14] = True
two_holes[4:12, 4:7] = False
two_holes[4:12, 8:12] = False
merged = two_holes.copy()
merged[4:12, 7] = False
true_model = Reference(two_holes, np.repeat(np.repeat(two_holes, 4, axis=0), 4, axis=1))
assert true_model.candidate("merged", merged).metrics["outline_lost_holes"] == 1
assert true_model.candidate("correct", two_holes).metrics["outline_lost_holes"] == 0
print("PASS: continuous subthreshold strokes, exact centroid attribution, Fourier phase/power and undefined symmetric tilt.")
