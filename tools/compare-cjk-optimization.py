#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2", "freetype-py==2.5.1", "matplotlib>=3.10,<4", "scikit-image>=0.25,<0.27", "ijson>=3.3,<4"]
# ///
"""Matched before/after statistics and static figures from a generation report.

uv run tools/compare-cjk-optimization.py --report build-font-sources/editorial-optimization.json
Never changes firmware or a device. Undefined central-white/tilt metrics stay
undefined. Resampling intervals describe glyph variation, not human confidence.
"""
import argparse
import ast
from collections import Counter
import csv
import json
from pathlib import Path
import re
import freetype as ft
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
import numpy as np
from scipy.stats import skew, kurtosis
from cjk_raster import render
from cjk_balance_metrics import measure

ROOT = Path(__file__).resolve().parent.parent


def decode(decision, key):
    shape = (decision["height"], decision["width"])
    raw = np.frombuffer(bytes.fromhex(decision[key+"_bits"]), dtype=np.uint8)
    if len(raw) != (np.prod(shape)+7)//8:
        raise ValueError("Invalid generation-report bitmap length")
    return np.unpackbits(raw)[:np.prod(shape)].reshape(shape).astype(bool)


def distribution(values, weights):
    valid = np.array([v is not None and np.isfinite(v) for v in values])
    a = np.array([v for v, good in zip(values, valid) if good], float)
    w = np.asarray(weights, float)[valid]
    result = {"valid": len(a), "undefined": len(values)-len(a)}
    if len(a):
        result.update({"mean": float(a.mean()), "std": float(a.std()), "max": float(a.max()),
                       "p50": float(np.percentile(a, 50)), "p95": float(np.percentile(a, 95)),
                       "p99": float(np.percentile(a, 99)),
                       "skewness": float(skew(a)) if a.std() > 1e-12 else None,
                       "excess_kurtosis": float(kurtosis(a)) if a.std() > 1e-12 else None,
                       "catalog_frequency_weighted_mean": float(np.average(a, weights=w)) if w.sum() else None})
    return result


def compare_diagnostics(paths, output):
    """Matched glyphs from existing independent audits, including regressions."""
    import ijson
    audits = []
    for path in paths:
        faces = {}
        with path.open("rb") as stream:
            for face in ijson.items(stream, "faces.item", use_float=True):
                embedded = next(c for c in face["candidates"] if c["name"] == "embedded")
                faces[face["role"]] = embedded["glyphs"]
        audits.append(faces)
    rows = []
    for role, before in audits[0].items():
        if role not in audits[1]:
            continue
        after = audits[1][role]
        common = sorted(set(before) & set(after))
        common = [c for c in common if (ord(c) < 127 if role == "Micro" else 0x3400 <= ord(c) <= 0x9fff)]
        if not common:
            continue
        for metric in sorted(set(before[common[0]]) & set(after[common[0]])):
            a, b = [[glyphs[c][metric] for c in common] for glyphs in (before, after)]
            if not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in a+b):
                continue
            rows.append({"role": role, "group": "ASCII" if role == "Micro" else "CJK", "matched_glyphs": len(common), "metric": metric,
                "before_mean": float(np.mean(a)), "after_mean": float(np.mean(b)),
                "before_p95": float(np.percentile(a, 95)), "after_p95": float(np.percentile(b, 95)),
                "before_max": float(max(a)), "after_max": float(max(b))})
    if not rows:
        raise ValueError("No common embedded glyphs in the two diagnostic reports")
    output.mkdir(parents=True, exist_ok=True)
    with (output / "independent-diagnostics.csv").open("w") as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    lines = ["# Independent diagnostic tradeoffs", "",
        "Matched actual embedded glyphs; all basic diagnostic metrics remain in independent-diagnostics.csv, including regressions.", "",
        "The native binary reference can itself erase thin strokes. Restoring outline strokes can increase binary-reference missing/spurious centerlines or white-channel losses. These increases are not hidden and are not alone proof of readability improvement or deterioration. True supersampled-outline support, white-axis closure and one-to-one hole correspondence are reported separately in report.json.", "",
        "This change is not an all-metric Pareto optimum. Physical optics, recognition and visual-weight judgments remain unmeasured.", "",
        "| Role / group | Blur MSE P95 before → after | Binary white loss P95 px | Binary missing centerline P95 fraction | Binary topology error P95 |",
        "| --- | ---: | ---: | ---: | ---: |"]
    keys = ("blur_mse", "white_channel_loss_px", "missing_centerline_fraction", "topology_count_error")
    for role in audits[0]:
        group = {r["metric"]: r for r in rows if r["role"] == role}
        if not all(k in group for k in keys):
            continue
        first = group[keys[0]]
        values = " | ".join(f"{group[k]['before_p95']:.5f} → {group[k]['after_p95']:.5f}" for k in keys)
        lines.append(f"| {role} / {first['group']} ({first['matched_glyphs']}) | {values} |")
    (output / "independent-diagnostics.md").write_text("\n".join(lines)+"\n")
    print(f"Matched independent diagnostic tradeoffs: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--report", type=Path)
    inputs.add_argument("--diagnostics", type=Path, nargs=2, metavar=("BEFORE", "AFTER"), help="Compare two existing full audits without rerasterizing")
    parser.add_argument("--output", type=Path, default=ROOT / "build-font-sources/cjk-optimized")
    args = parser.parse_args()
    if args.diagnostics:
        compare_diagnostics(args.diagnostics, args.output)
        return
    plan = json.loads(args.report.read_text())
    source = Path(plan.get("source_directory", ROOT / "build-font-sources"))
    args.output.mkdir(parents=True, exist_ok=True)
    literals = re.findall(r'"(?:[^"\\]|\\.)*"', (ROOT / "components/note4_app/include/note4_strings.inc").read_text())
    frequency = Counter("".join(ast.literal_eval(s) for s in literals))
    faces, rows, examples = [], [], {}
    rng = np.random.default_rng(20261005)
    sizes = {"Caption": 14, "Label": 16, "Navigation": 18, "Selected": 18, "Heading": 22, "Compact": 16}
    for face in plan["faces"]:
        decisions = face["cjk_decisions"]
        if not decisions:
            continue
        role, size = face["name"], sizes[face["name"]]
        weight = "Bold" if role in ("Selected", "Heading", "Compact") else "Regular"
        f = ft.Face(str(source / f"NotoSansCJKsc-{weight}.otf"))
        f.ui_size = size
        members = {"before": [], "after": []}
        for d in decisions:
            ref, outline = render(f, d["char"], d["width"], d["height"], size-1, 16, ft.FT_LOAD_NO_HINTING, include_outline=True)
            for state in members:
                mask = decode(d, state)
                metrics = measure(mask, ref, outline)
                for key, value in d[state].items():
                    if value is None:
                        assert metrics[key] is None
                    else:
                        np.testing.assert_allclose(metrics[key], value, atol=1e-12, rtol=0)
                row = {"role": role, "char": d["char"], "state": state,
                       "source_frequency": frequency[d["char"]], **metrics}
                members[state].append(row)
                rows.append(row)
            if role == "Caption" and d["char"] in "信禁动试见还白":
                examples[d["char"]] = (decode(d, "before"), ref, decode(d, "after"), members["before"][-1], members["after"][-1])
        result = {"role": role, "glyphs": len(decisions), "changed": face["cjk_changed"], "states": {}, "paired": {}}
        for state, records in members.items():
            result["states"][state] = {key: distribution([r[key] for r in records], [r["source_frequency"] for r in records])
                for key in records[0] if key not in ("role", "char", "state", "source_frequency")}
            result["states"][state]["strong_extra_glyph_count"] = sum(r["strong_extra"] > 0 for r in records)
            for kind in ("ink", "core"):
                xy = np.array([[r[kind+"_dx"], r[kind+"_dy"]] for r in records])
                result["states"][state][kind+"_vector"] = {"mean": xy.mean(axis=0).tolist(), "covariance": np.cov(xy, rowvar=False, ddof=0).tolist()}
                for threshold in (.5, 1.):
                    result["states"][state][f"{kind}_prob_gt_{threshold}"] = float(np.mean([r[kind+"_drift"] > threshold for r in records]))
        indices = rng.integers(0, len(decisions), size=(2000, len(decisions)))
        for key in ("ink_drift", "core_drift", "darkness_drift", "phase_drift", "orientation_js_bits", "direction_extra_range_pp"):
            valid = [i for i in range(len(decisions)) if all(members[s][i][key] is not None for s in members)]
            delta = np.array([members["after"][i][key]-members["before"][i][key] for i in valid])
            if len(valid) == len(decisions):
                boot = delta[indices].mean(axis=1)
            else:
                boot = delta[rng.integers(0, len(valid), size=(2000, len(valid)))].mean(axis=1) if valid else None
            result["paired"][key] = {"valid": len(valid), "mean_change": float(delta.mean()) if valid else None,
                "improved": int((delta < -1e-9).sum()), "worsened": int((delta > 1e-9).sum()),
                "glyph_resampling_mean_95_interval": np.percentile(boot, [2.5, 97.5]).tolist() if boot is not None else None}
        faces.append(result)
        print(f"Measured {role}: {len(decisions)} matched CJK glyphs", flush=True)
    report = {"reference": "independent 16x unhinted continuous coverage, not human ground truth",
        "limitations": "Gaussian core/salience and Fourier metrics are uncalibrated. Some unoptimized diagnostics can trade off. Source frequency is not usage probability; no physical panel, human or power measurements.",
        "storage_bytes": plan["total_glyph_and_index_bytes"], "faces": faces}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+"\n")
    with (args.output / "glyph-statistics.csv").open("w") as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    labels = [f"{f['role']} ({sizes[f['role']]}px)" for f in faces]
    figure, axes = plt.subplots(1, 3, figsize=(14, 4.9), layout="constrained")
    for ax, key, title in zip(axes, ("ink_drift", "core_drift", "phase_drift"), ("Ink centroid P95 (px)", "Central ink centroid P95 (px)", "Fourier phase displacement P95 (px)")):
        y = np.arange(len(faces))
        for shift, state, color, label in [(-.18, "before", "#777777", "Before"), (.18, "after", "#2279b0", "After")]:
            values = [f["states"][state][key]["p95"] for f in faces]
            ax.barh(y+shift, values, .32, color=color, label=label)
            for row, value in enumerate(values):
                ax.text(value+.015, row+shift, f"{value:.3f}", va="center", fontsize=9)
        ax.set_yticks(y, labels if ax is axes[0] else [])
        ax.invert_yaxis()
        ax.set_xlim(0, max(f["states"][s][key]["p95"] for f in faces for s in ("before", "after"))*1.2)
        ax.set_title(title, fontsize=11)
        ax.set_xlabel("Native pixels; lower is closer to outline")
        ax.spines[["top", "right"]].set_visible(False)
    axes[0].legend(frameon=False)
    counts = sorted({f["glyphs"] for f in faces})
    matched = str(counts[0]) if len(counts) == 1 else "/".join(map(str, counts))
    figure.suptitle(f"CJK offline optimization — {matched} matched glyphs per role", fontsize=14)
    figure.savefig(args.output / "balance-improvement.png", dpi=160)
    plt.close(figure)
    figure, axes = plt.subplots(2, 3, figsize=(15, 8.7), layout="constrained")
    panels = [("outline_stroke_distance", "p95", 1., "True-outline stroke distance (P95, px)"),
              ("outline_white_distance", "mean", 1., "True-outline white displacement (mean, px)"),
              ("outline_white_closed_fraction", "mean", 100., "Blocked resolvable white axes (mean, %)"),
              ("outline_lost_holes", "mean", 1., "Lost resolvable outline holes (mean, count)"),
              ("orientation_js_bits", "p95", 1., "Fourier orientation JSD (P95, bits)"),
              ("high_complex_rms", "p95", 1., "High-frequency complex residual (P95, unit mass)")]
    for index, (ax, (key, stat, factor, title)) in enumerate(zip(axes.flat, panels)):
        y = np.arange(len(faces))
        all_values = []
        for shift, state, color, label in [(-.18, "before", "#777777", "Before"), (.18, "after", "#2279b0", "After")]:
            values = [factor*f["states"][state][key][stat] for f in faces]
            all_values.extend(values)
            ax.barh(y+shift, values, .32, color=color, label=label)
        ax.set_yticks(y, labels if index % 3 == 0 else [])
        ax.invert_yaxis()
        ax.set_xlim(0, max(max(all_values)*1.12, .001))
        ax.set_title(title, fontsize=11)
        ax.spines[["top", "right"]].set_visible(False)
    axes[0, 0].legend(frameon=False)
    figure.suptitle("Outline protection and independent spectral tradeoffs — no combined quality score", fontsize=14)
    figure.savefig(args.output / "outline-spectral-tradeoffs.png", dpi=160)
    plt.close(figure)
    props = FontProperties(fname=str(source / "NotoSansCJKsc-Regular.otf"))
    chars = list("信禁动试见还白")
    figure, axes = plt.subplots(len(chars), 3, figsize=(10, 14), layout="constrained")
    for row, char in enumerate(chars):
        before, ref, after, mb, ma = examples[char]
        for column, image in enumerate((before, ref, after)):
            ax = axes[row, column]
            ax.imshow(1-image.astype(float), cmap="gray", vmin=0, vmax=1, interpolation="nearest")
            ax.axis("off")
            if not row:
                ax.set_title(("Before · 1bpp", "Outline coverage · 16x", "After · 1bpp")[column], fontsize=12)
            if column in (0, 2):
                m = mb if column == 0 else ma
                ax.text(.5, -.02, f"core Δ {m['core_drift']:.3f}px · strong extras {m['strong_extra']}", transform=ax.transAxes, ha="center", va="top", fontsize=10)
        axes[row, 0].text(-.12, .5, char, transform=axes[row, 0].transAxes, fontproperties=props, fontsize=24, ha="right", va="center")
    figure.suptitle("14px regular: restored strokes and conservative unchanged cases", fontsize=14)
    figure.savefig(args.output / "glyph-improvement.png", dpi=160)
    plt.close(figure)
    lines = ["# CJK offline optimization", "", report["reference"], "", report["limitations"], "",
             f"{matched} matched CJK characters per role; measurements are paired, not population/readability estimates.", "",
             "| Role | Changed | Ink P95 px before → after | Core P95 px before → after | Strong-extra characters before → after |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for f in faces:
        b, a = f["states"]["before"], f["states"]["after"]
        lines.append(f"| {f['role']} | {f['changed']}/{f['glyphs']} | {b['ink_drift']['p95']:.3f} → {a['ink_drift']['p95']:.3f} | {b['core_drift']['p95']:.3f} → {a['core_drift']['p95']:.3f} | {b['strong_extra_glyph_count']} → {a['strong_extra_glyph_count']} |")
    lines += ["", f"Packed font + indices: {report['storage_bytes']:,} bytes. Runtime representation and layout metrics unchanged.", "",
        "Selection protects the six primary metrics, true-outline stroke distance, white-axis closure, one-to-one hole correspondence and the existing native count protection. Mean white displacement admits half a reference sample (1/32px at 16x); raw changes remain visible. Missing white-axis samples stay undefined. This is finite-precision constrained improvement, not zero regression in all diagnostics.", "",
        "See report.json for mean/P95/P99, skewness, kurtosis, covariance, source-frequency weighting, paired resampling and Fourier/directional tradeoffs; glyph-statistics.csv retains individual results."]
    (args.output / "summary.md").write_text("\n".join(lines)+"\n")
    print(f"Static comparisons: {args.output}")


if __name__ == "__main__":
    main()
