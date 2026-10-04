#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2"]
# ///
"""Offline optimizer/packing tests; no panel-quality gate."""
import importlib.util
import json
import sys
from pathlib import Path
import numpy as np

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
if len(sys.argv) > 1:
    report = json.loads(Path(sys.argv[1]).read_text())
    assert len(report["faces"]) == 7
    for face in report["faces"]:
        chosen = face["selected"]
        native = next(r for r in face["candidates"] if r["scale"] == 1 and r["threshold"] == 128)
        assert chosen in face["pareto"]
        assert chosen["distortion"] <= native["distortion"]
        assert chosen["bytes"] <= face["original_bitmap_bytes"]
        assert native["fallbacks"] == 0
print("PASS: editorial lossless row packing, topology and Pareto selection.")
