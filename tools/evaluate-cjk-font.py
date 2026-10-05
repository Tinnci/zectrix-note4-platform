#!/usr/bin/env python3
# /// script
# dependencies = ["pillow==11.3.0", "numpy>=2,<3", "scipy>=1.14,<2", "freetype-py==2.5.1", "scikit-image>=0.25,<0.27", "gudhi>=3.10,<4", "phasepack==1.5"]
# ///
"""Offline CJK diagnostics, not a readability gate or firmware generator."""
import argparse
import ast
import importlib.util
import json
import math
import re
import random
import time
from pathlib import Path

import freetype as ft
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi
from skimage.morphology import skeletonize
from cjk_raster import render
from cjk_metrics import Features, compare, confusion, robustness
from cjk_context import corpus, load_embedded, page_context, contextual_neighbors, extended_summaries
from cjk_measurements import compression, measurements, DistsModel

spec = importlib.util.spec_from_file_location("editorial", Path(__file__).with_name("generate-editorial-font.py"))
editorial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(editorial)
PAIRS = [("未", "末"), ("土", "士"), ("己", "已"), ("已", "巳")]
SAMPLE = "主页电子书局域网传书应用随身工具时钟待机画报连接离线异常打开中不可用阅读设置蓝牙刷新睡眠日历警醒鬱體龍龜，。！？：；（）「」…"
GROUPS = {"confusable": "未末土士己已巳", "dense_stress": "鬱體龍龜警醒",
          "punctuation": "，。！？：；（）「」…"}
FACES = [("Caption", 14, "Regular"), ("Label", 16, "Regular"),
         ("Navigation", 18, "Regular"), ("Selected", 18, "Bold"),
         ("Heading", 22, "Bold"), ("Compact", 16, "Bold"), ("Micro", 12, "Regular")]


def catalog_chars():
    literals = re.findall(r'"(?:[^"\\]|\\.)*"',
                         (editorial.ROOT / "components/note4_app/include/note4_strings.inc").read_text())
    chars = set("".join(ast.literal_eval(item) for item in literals))
    chars.update(chr(cp) for cp in range(32, 127))
    chars.update("风从海上来…")
    return {c for c in chars if ord(c) >= 32}


def summary(values):
    return {"mean": float(np.mean(values)), "p95": float(np.percentile(values, 95)), "max": float(max(values))}


def holes(mask):
    return ndi.binary_fill_holes(mask, structure=np.ones((3, 3))) & ~mask


def unmatched_holes(source, target):
    labels, count = ndi.label(holes(source), np.ones((3, 3)))
    target_holes = holes(target)
    return sum(not target_holes[labels == i].any() for i in range(1, count+1))


def morphology(mask):
    """Descriptive values, not errors; smaller is not necessarily better."""
    skeleton = skeletonize(mask)
    widths = 2 * ndi.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1][skeleton]
    coords = np.argwhere(mask)
    if not coords.size:
        return {"ink_height": 0, "ink_width": 0, "ink_bottom": 0,
                "left_bearing": mask.shape[1], "right_bearing": 0,
                "stroke_width_p5": 0., "stroke_width_p95": 0., "gap_width_p5": 0.}
    y0, x0 = coords.min(axis=0)
    y1, x1 = coords.max(axis=0)+1
    white = ~mask[y0:y1, x0:x1]
    white_skeleton = skeletonize(white)
    gap_widths = 2 * ndi.distance_transform_edt(np.pad(white, 1))[1:-1, 1:-1][white_skeleton]
    return {"ink_height": int(y1-y0), "ink_width": int(x1-x0), "ink_bottom": int(y1),
            "left_bearing": int(x0), "right_bearing": int(mask.shape[1]-x1),
            "stroke_width_p5": float(np.percentile(widths, 5)),
            "stroke_width_p95": float(np.percentile(widths, 95)),
            "gap_width_p5": float(np.percentile(gap_widths, 5)) if gap_widths.size else 0.}


def diagnostics(mask, reference, shape=None, ref_shape=None):
    """Keep metrics separate: no uncalibrated weighted readability score."""
    ref_mask = reference >= .5
    blurred = ndi.gaussian_filter(mask.astype(float), .65)
    ref_blurred = ndi.gaussian_filter(reference, .65)
    skeleton = skeletonize(mask)
    ref_skeleton = skeletonize(ref_mask)
    precision = float((skeleton & ref_mask).sum()) / max(1, int(skeleton.sum()))
    recall = float((ref_skeleton & mask).sum()) / max(1, int(ref_skeleton.sum()))
    cldice_loss = (1 - 2 * precision * recall / max(1e-9, precision + recall)
                   if mask.any() or ref_mask.any() else 0.0)
    # Compare width distributions on skeletons, not arbitrary background margins.
    stroke = 2 * ndi.distance_transform_edt(mask)[skeleton]
    ref_stroke = 2 * ndi.distance_transform_edt(ref_mask)[ref_skeleton]
    # Interior white channels: restrict to the common ink bounding box.
    coords = np.argwhere(ref_mask)
    gap_loss = gap_expansion = 0.0
    if coords.size:
        y0, x0 = coords.min(axis=0)
        y1, x1 = coords.max(axis=0) + 1
        roi = np.s_[y0:y1, x0:x1]
        white = ~ref_mask[roi]
        gaps = ndi.distance_transform_edt(~mask)[roi]
        ref_gaps = ndi.distance_transform_edt(~ref_mask)[roi]
        gap_loss = float(np.mean(np.maximum(0, ref_gaps[white] - gaps[white]))) if white.any() else 0.0
        gap_expansion = float(np.mean(np.maximum(0, gaps[white] - ref_gaps[white]))) if white.any() else 0.0
    topology_error = sum(abs(a-b) for a, b in zip(editorial.topology(mask), editorial.topology(ref_mask)))
    if shape is None:
        shape = morphology(mask)
    if ref_shape is None:
        ref_shape = morphology(ref_mask)
    return {"blur_mse": float(np.mean((blurred-ref_blurred)**2)),
            "area_relative_error": abs(float(mask.sum())-float(reference.sum())) / max(1., float(reference.sum())),
            "centerline_loss": cldice_loss, "topology_count_error": topology_error,
            "stroke_width_median_error": abs((float(np.median(stroke)) if stroke.size else 0.)
                                            - (float(np.median(ref_stroke)) if ref_stroke.size else 0.)),
            "white_channel_loss_px": gap_loss, "white_channel_expansion_px": gap_expansion,
            "missing_centerline_fraction": 1-recall if ref_skeleton.any() else 0.,
            "spurious_centerline_fraction": 1-precision if skeleton.any() else 0.,
            "lost_holes": unmatched_holes(ref_mask, mask),
            "extra_holes": unmatched_holes(mask, ref_mask),
            "ink_height_error_px": abs(shape["ink_height"]-ref_shape["ink_height"]),
            "ink_bottom_error_px": abs(shape["ink_bottom"]-ref_shape["ink_bottom"])}


def test_page(masks, title, output, width, height):
    """Native-pixel calibration patterns and text; never resize the raster."""
    page = Image.new("1", (width, height), 1)
    draw = ImageDraw.Draw(page)
    draw.text((8, 6), title, fill=0, font=ImageFont.load_default(size=10))
    y = 26
    texts = ["未末 土士 己已巳", "主页电子书连接刷新", "鬱體龍龜警醒", "，。！？：；（）「」…", "0123456789 BT WI-FI"]
    for text in texts:
        x = 8
        row_height = max(m.shape[0] for m in masks.values())
        for char in text:
            if char not in masks:
                continue
            mask = masks[char]
            if x+mask.shape[1] > width-8:
                x, y = 8, y+row_height+4
            image = Image.fromarray(np.where(mask, 0, 255).astype(np.uint8)).convert("1", dither=Image.Dither.NONE)
            page.paste(image, (x, y))
            x += mask.shape[1]+1
        y += row_height+8
    y = max(y+6, height-68)
    # Line widths/gaps of 1, 2, 3px and a 1px diagonal reveal edge spread.
    for index, thickness in enumerate((1, 2, 3)):
        x = 8+index*50
        for n in range(4):
            draw.rectangle((x+n*thickness*2, y, x+n*thickness*2+thickness-1, y+24), fill=0)
        draw.line((x+30, y+24, x+48, y), fill=0, width=1)
    draw.rectangle((width-70, y, width-10, y+24), fill=0)
    draw.rectangle((8, height-22, width-9, height-9), outline=0)
    page.save(output)
    # Canonical P4 accepted by the existing edge-page server. PBM: 1=black,
    # MSB first, independently padded rows (portrait width is not byte-aligned).
    black = ~np.asarray(page, dtype=bool)
    output.with_suffix(".pbm").write_bytes(f"P4\n{width} {height}\n".encode()
                                         + np.packbits(black, axis=1).tobytes())


def pair_distance(a, b, sigma):
    return float(np.mean(np.abs(ndi.gaussian_filter(a.astype(float), sigma)
                                 - ndi.gaussian_filter(b.astype(float), sigma))))


def objective(candidate):
    # Independent dimensions, including tail risk; no arbitrary weighted sum.
    metrics = candidate.get("comparison_metrics", candidate["metrics"])
    return (candidate.get("catalog_with_index_bytes", candidate["bitmap_bytes"]), metrics["blur_mse"]["mean"],
            metrics["blur_mse"]["p95"], metrics["white_channel_loss_px"]["p95"],
            metrics["topology_count_error"]["p95"], metrics["centerline_loss"]["p95"])


def frontier(candidates):
    return [c["name"] for c in candidates if not any(
        all(a <= b for a, b in zip(objective(other), objective(c))) and
        any(a < b for a, b in zip(objective(other), objective(c)))
        for other in candidates)]


def write_summary(report, output):
    lines = ["# CJK raster diagnostics", "", report["limitations"], "",
             f"Scope: {report['scope']}; {report['catalog_count']} catalog characters.", "",
             "Evaluated catalog storage includes the existing 12-byte glyph index; sample-only runs are not full-font sizes. Metrics below use UI CJK",
             "characters (ASCII for Micro), not a frequency-weighted readability score.", "",
             "| Role | Method | Evaluated catalog KiB | Blur error P95 | Missing centerline P95 | White gap loss P95 (px) |",
             "|---|---|---:|---:|---:|---:|"]
    for face in report["faces"]:
        for candidate in face["candidates"]:
            metrics = candidate["groups"]["ui_cjk" if "ui_cjk" in candidate["groups"] else "ui_ascii"]
            values = [candidate["catalog_with_index_bytes"]/1024,
                      metrics["blur_mse"]["p95"], metrics["missing_centerline_fraction"]["p95"],
                      metrics["white_channel_loss_px"]["p95"]]
            lines.append(f"| {face['role']} | {candidate['name']} | " + " | ".join(f"{v:.4f}" for v in values) + " |")
    lines += ["", "## Exploratory frontiers", ""]
    lines += [f"- {f['role']}: {', '.join(f['diagnostic_frontier'])}" for f in report["faces"]]
    lines += ["", "Frontiers use the common glyph intersection when present; stress samples are unweighted.",
              "Panel pages carry neutral trial IDs. Keep report.json's method mapping hidden during A/B tests.",
              "PNG and canonical P4 PBM encode identical native pixels. No panel measurements were made."]
    if "metric_families" in report:
        lines += ["", "## Extended independent metrics", "",
                  "Per-glyph raw values, reference errors, frequency-weighted summaries, directional widths and sensitivity curves are in report.json.",
                  "Embedded glyphs are decoded from the actual header, including native topology fallbacks; missing stress glyphs are not invented.",
                  "Frontiers compare the same character intersection. They retain the original exploratory objective, not every new metric or a universal winner.", ""]
        lines += [f"- {k}: {v['status']} — {v['note']}" for k, v in report["metric_families"].items()]
        lines += ["", "## Embedded font: weight, balance and structure", "",
                  "These are whole evaluated-set proxies against the matching outline, not subjective ratings. Signed means and absolute tails are separate.", "",
                  "| Role | Glyphs | Normalized weight error mean | Centroid drift P95 px | SSIM mean | Collapsed directed neighbors |",
                  "|---|---:|---:|---:|---:|---:|"]
        for face in report["faces"]:
            candidate = next(c for c in face["candidates"] if c["name"] == "embedded")
            metrics = candidate["extended_summary"]["unweighted"]
            values = [metrics["errors.weight_error.normalized_effective_width"]["mean"],
                      metrics["errors.centroid_drift_px"]["p95"], metrics["errors.image.ssim"]["mean"]]
            collapsed = sum(n["collapsed"] for g in candidate["confusion"].values() for n in g["nearest"])
            numbers = " | ".join("n/a" if v is None else f"{v:.4f}" for v in values)
            lines.append(f"| {face['role']} | {candidate['evaluated_glyph_count']} | {numbers} | {collapsed} |")
        if "page_context" in report:
            context = report["page_context"]
            lines += ["", f"Host text traces: {len(context['pages'])} pages; no user exposure probabilities assigned.",
                      "Cross-script ratios are separated by role/source/size/style; same-page/line/word-run associations and adjacent jumps remain in report.json."]
        lines += ["", "## Real measurement availability", ""]
        lines += [f"- {k}: {v['status']}" for k, v in report["measurements"].items()]
    output.write_text("\n".join(lines)+"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--source", type=Path)
    inputs.add_argument("--summarize-report", type=Path, help="Reformat an existing report without rerasterizing")
    inputs.add_argument("--enrich-report", type=Path, help="Recompute corpus/context summaries from an existing quantitative report; no glyph rerasterization")
    parser.add_argument("--output", type=Path, default=Path("build-font-sources/cjk-evaluation"))
    parser.add_argument("--sample-only", action="store_true", help="Fast diagnostic sample instead of the complete UI catalog")
    parser.add_argument("--roles", nargs="+", choices=[f[0] for f in FACES], help="Evaluate selected roles; default: all seven")
    parser.add_argument("--methods", nargs="+", choices=["native-mono", "autohint-mono", "native-gray", "unhinted-2x", "unhinted-4x", "pillow-4x", "embedded"],
                        help="Evaluate selected methods; default: all seven. Use embedded for a full post-generation audit without recomputing unchanged controls")
    parser.add_argument("--pages", type=Path, help="Native preview directory with PBMs and host .text.json paint traces")
    parser.add_argument("--measurements", type=Path, help="JSON with recognition, weight_judgments, energy and refreshes")
    parser.add_argument("--dists-model", type=Path, help="Trusted local pretrained TorchScript DISTS; requires uv --with torch")
    args = parser.parse_args()
    if args.methods:
        args.methods = list(dict.fromkeys(args.methods))
    if args.summarize_report:
        write_summary(json.loads(args.summarize_report.read_text()), args.summarize_report.with_name("summary.md"))
        return
    if args.enrich_report:
        report = json.loads(args.enrich_report.read_text())
        report["corpus"] = corpus(editorial.ROOT / "components/note4_app/include/note4_strings.inc")
        if args.pages:
            report["page_context"] = page_context(args.pages, load_embedded(editorial.ROOT / "components/ui/font/editorial_font.h"))
        for face in report["faces"]:
            for candidate in face["candidates"]:
                candidate["extended_summary"] = extended_summaries(candidate["extended_glyphs"], report["corpus"], face["role"], report.get("page_context"))
                candidate["contextual_neighbors"] = contextual_neighbors(candidate["confusion"], report["corpus"], report.get("page_context"))
        if args.measurements:
            report["measurements"] = measurements(json.loads(args.measurements.read_text()), args.measurements.parent)
        report["enrichment_note"] = "Updated corpus/context association summaries; original glyph measurements were not rerasterized. Match font and preview sources before interpreting new associations."
        args.enrich_report.write_text(json.dumps(report, ensure_ascii=False, separators=(",", ":"), allow_nan=False)+"\n")
        write_summary(report, args.enrich_report.with_name("summary.md"))
        print(f"Updated independent corpus/context summaries: {args.enrich_report}")
        return
    args.output.mkdir(parents=True, exist_ok=True)
    catalog = catalog_chars()
    embedded = load_embedded(editorial.ROOT / "components/ui/font/editorial_font.h")
    source_corpus = corpus(editorial.ROOT / "components/note4_app/include/note4_strings.inc")
    dists = DistsModel(args.dists_model) if args.dists_model else None
    stress = set(SAMPLE + "".join(a+b for a, b in PAIRS))
    chars = sorted(stress if args.sample_only else catalog | stress)
    report = {"reference": "16x unhinted outline coverage; independent of candidates, not human ground truth",
              "limitations": "No automatic selection. Gaussian blur is a sensitivity model, not measured EPD optics/ghosting. Count topology and skeleton proxies do not prove readability.",
              "freetype": list(ft.version()), "scope": "sample" if args.sample_only else "full UI catalog plus stress sample",
              "catalog_count": len(catalog), "methods": args.methods or ["native-mono", "autohint-mono", "native-gray", "unhinted-2x", "unhinted-4x", "pillow-4x", "embedded"],
              "faces": [], "panel_trials": [], "corpus": source_corpus,
              "measurements": measurements(json.loads(args.measurements.read_text()) if args.measurements else None,
                                           args.measurements.parent if args.measurements else Path(".")),
              "metric_families": {
                  "pixel_contour": {"status": "computed", "note": "MSE/PSNR, symmetric boundary and signed-distance errors; exact PSNR uses a flag, not JSON infinity"},
                  "perceptual_structure": {"status": "computed", "note": "SSIM, native-size adaptive MS-SSIM and FSIM with phasepack PC; tiny-glyph validation is still required"},
                  "dists": {"status": "computed" if dists else "needs_model", "note": "Optional trusted local pretrained model; no downloads or random-weight scores"},
                  "weight_direction_balance": {"status": "computed", "note": "Directional normal-ray width, density, effective width, centroid/moments/quadrants; no forced equal CJK/Latin area"},
                  "topology": {"status": "computed", "note": "Counts, holes, component correspondences, missing patches and coverage-filtration bottleneck distances"},
                  "confusion": {"status": "computed", "note": "All-glyph nearest neighbors, outline-referenced margins and distinctive regions, not human confusion probabilities. Contextual pair keys link to shared corpus/page graphs; page cooccurrence may span font roles and is not a usage probability"},
                  "robustness": {"status": "computed", "note": "Explicit blur/contrast/x-y phase/residue scenarios; uncalibrated sensitivity, not panel physics"},
                  "frequency_context": {"status": "computed", "note": "Source frequencies and adjacency/cooccurrence; source occurrences are not usage probabilities"},
                  "compression_cost": {"status": "computed", "note": "Actual raw/trim/RLE/zlib sizes and host decode timing; device RAM/time/power remain unmeasured"},
                  "optics_human_power": {"status": "measured_inputs" if args.measurements else "needs_measurement", "note": "Explicit data ingestion; missing measurements never become zero error"}}}
    if args.pages:
        report["page_context"] = page_context(args.pages, embedded)
        if report["page_context"]["status"] != "computed":
            raise ValueError("No host text traces found; regenerate native previews first")
    preview_rows = []
    for role, size, weight in FACES:
        if args.roles and role not in args.roles:
            continue
        path = args.source / f"NotoSansCJKsc-{weight}.otf"
        face = ft.Face(str(path))
        face.ui_size = size
        pillow = ImageFont.truetype(str(path), size)
        pillow4 = ImageFont.truetype(str(path), size*4)
        height, baseline = size + (6 if size == 22 else 4), size if size == 12 else size-1
        face_chars = [chr(cp) for cp in range(32, 127)] if role == "Micro" else chars
        variants = {name: {} for name in ["native-mono", "autohint-mono", "native-gray", "unhinted-2x", "unhinted-4x", "pillow-4x"]
                    if name in report["methods"]}
        references = {}
        for char in face_chars:
            if not face.get_char_index(ord(char)):
                raise ValueError(f"Missing glyph U+{ord(char):04X} in {path.name}")
            width = max(1, math.ceil(pillow.getlength(char)))
            references[char] = render(face, char, width, height, baseline, 16, ft.FT_LOAD_NO_HINTING)
            if "native-mono" in variants:
                variants["native-mono"][char] = render(face, char, width, height, baseline, 1, ft.FT_LOAD_TARGET_MONO | ft.FT_LOAD_MONOCHROME, True) >= .5
            if "autohint-mono" in variants:
                variants["autohint-mono"][char] = render(face, char, width, height, baseline, 1, ft.FT_LOAD_FORCE_AUTOHINT | ft.FT_LOAD_TARGET_MONO | ft.FT_LOAD_MONOCHROME, True) >= .5
            if "native-gray" in variants:
                variants["native-gray"][char] = render(face, char, width, height, baseline, 1, ft.FT_LOAD_TARGET_NORMAL) >= 128/255
            for scale in (2, 4):
                if f"unhinted-{scale}x" in variants:
                    variants[f"unhinted-{scale}x"][char] = render(face, char, width, height, baseline, scale, ft.FT_LOAD_NO_HINTING) >= 128/255
            if "pillow-4x" in variants:
                variants["pillow-4x"][char] = editorial.raster(pillow4, ord(char), width, height, baseline, 4) >= 128/255
        if "embedded" in report["methods"]:
            variants["embedded"] = {c: m for c, m in embedded[role]["masks"].items() if c in references}
        for c, mask in variants.get("embedded", {}).items():
            if mask.shape != references[c].shape:
                raise ValueError(f"Embedded/reference advance or line height differ: {role} {c!r}; use matching font sources")
        common = sorted(set.intersection(*(set(masks) for masks in variants.values())))
        if not common:
            raise ValueError(f"No common comparison glyphs for {role}")
        results = []
        reference_shapes = {char: morphology(ref >= .5) for char, ref in references.items()}
        reference_features = {char: Features(ref) for char, ref in references.items()}
        groups = {**GROUPS, "ui_ascii": "".join(c for c in face_chars if c in catalog and ord(c) < 127),
                  "ui_cjk": "".join(c for c in face_chars if c in catalog and 0x3400 <= ord(c) <= 0x9fff)}
        for name, masks in variants.items():
            started = time.perf_counter()
            shapes = {char: morphology(mask) for char, mask in masks.items()}
            glyphs = {char: diagnostics(mask, references[char], shapes[char], reference_shapes[char])
                      for char, mask in masks.items()}
            aggregates = {metric: summary([g[metric] for g in glyphs.values()]) for metric in next(iter(glyphs.values()))}
            extended = {}
            for char, mask in masks.items():
                features = Features(mask)
                extended[char] = {"descriptors": features.descriptors(), "errors": compare(features, reference_features[char])}
                if dists:
                    extended[char]["dists_distance"] = dists.distance(mask, references[char])
            all_neighbors = confusion({c: m for c, m in masks.items() if not c.isspace()}, references)
            curves = {c: robustness(m, references[c]) for c, m in masks.items()}
            extended_summary = extended_summaries(extended, source_corpus, role, report.get("page_context"))
            pairs = []
            for a, b in PAIRS:
                if a not in masks or b not in masks:
                    continue
                for sigma in (.0, .65, 1.0):
                    ref_distance = pair_distance(references[a], references[b], sigma)
                    distance = pair_distance(masks[a], masks[b], sigma)
                    pairs.append({"pair": a+b, "blur_sigma_px": sigma, "distance": distance,
                                  "reference_distance": ref_distance, "retention": distance / max(1e-9, ref_distance),
                                  "collapsed": bool(np.array_equal(masks[a], masks[b]))})
            catalog_packed = sum(len(editorial.pack(m)[0]) for c, m in masks.items() if c in catalog)
            catalog_untrimmed = sum((m.size+7)//8 for c, m in masks.items() if c in catalog)
            results.append({"name": name, "bitmap_bytes": sum(len(editorial.pack(m)[0]) for m in masks.values()),
                            "catalog_bitmap_bytes": sum(len(editorial.pack(m)[0]) for c, m in masks.items() if c in catalog),
                            "catalog_with_index_bytes": sum(len(editorial.pack(m)[0])+12 for c, m in masks.items() if c in catalog),
                            "lossless_row_trim_saved_bytes": catalog_untrimmed-catalog_packed,
                            "comparison_metrics": {metric: summary([glyphs[c][metric] for c in common]) for metric in aggregates},
                            "extended_glyphs": extended,
                            "extended_summary": extended_summary,
                            "confusion": all_neighbors, "robustness": curves,
                            "contextual_neighbors": contextual_neighbors(all_neighbors, source_corpus, report.get("page_context")),
                            "compression": compression(masks), "host_analysis_seconds": time.perf_counter()-started,
                            "evaluated_glyph_count": len(masks),
                            "missing_from_evaluated_set": sorted(set(references)-set(masks)),
                            "full_embedded_storage_bytes": embedded[role]["bitmap_bytes"]+embedded[role]["index_bytes"] if name == "embedded" else None,
                            "metrics": aggregates, "pairs": pairs,
                            "groups": {group: {metric: summary([glyphs[c][metric] for c in members if c in glyphs])
                                               for metric in aggregates} for group, members in groups.items()
                                       if any(c in glyphs for c in members)},
                            "morphology": {metric: summary([g[metric] for g in shapes.values()])
                                           for metric in next(iter(shapes.values()))},
                            "glyph_morphology": shapes,
                            "worst_blur": sorted(glyphs, key=lambda c: glyphs[c]["blur_mse"], reverse=True)[:8],
                            "glyphs": glyphs})
            print(f"  {role}/{name}: {len(masks)} glyphs, {results[-1]['host_analysis_seconds']:.1f}s host analysis", flush=True)
            preview_rows.append((f"{role} {size}px {name}", [masks[c] for c in
                                ("0123456789BT" if role == "Micro" else "未末土士己已巳主页鬱體龍龜") if c in masks]))
            for width, height_px in [(300, 400), (400, 300)]:
                trial = f"T{len(preview_rows):02d}-{width}"
                filename = f"panel-{trial}.png"
                test_page(masks, f"Note4 {trial} {size}px", args.output / filename, width, height_px)
                report["panel_trials"].append({"id": trial, "role": role, "candidate": name,
                                              "width": width, "height": height_px, "png": filename,
                                              "pbm": Path(filename).with_suffix(".pbm").name})
        report["faces"].append({"role": role, "size": size, "weight": weight, "glyph_count": len(face_chars),
                                "evaluated_catalog_count": sum(c in catalog for c in face_chars), "candidates": results,
                                "comparison_glyphs": common, "diagnostic_frontier": frontier(results)})
        print(f"Evaluated {role}: {len(face_chars)} source glyphs × {len(variants)} methods; common={len(common)}", flush=True)
    order = [t["id"] for t in report["panel_trials"]]
    random.Random(0).shuffle(order)
    report["example_trial_order"] = order
    report["trial_order_note"] = "Example order only; randomize within matched role/orientation blocks separately for each participant. Keep candidate mapping hidden. These pages are not timed recognition trials."
    # Large per-glyph/page data stays compact; summary.md is the readable entry.
    args.output.joinpath("report.json").write_text(json.dumps(report, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n")
    write_summary(report, args.output / "summary.md")
    # Nearest-neighbor enlargement exposes individual output pixels, not smoothed mockups.
    sheet = Image.new("RGB", (1400, len(preview_rows)*110), "white")
    draw = ImageDraw.Draw(sheet)
    label = ImageFont.load_default(size=16)
    for row, (name, masks) in enumerate(preview_rows):
        draw.text((8, row*110+8), name, fill="black", font=label)
        x = 320
        for mask in masks:
            image = Image.fromarray(np.where(mask, 0, 255).astype(np.uint8)).resize((mask.shape[1]*3, mask.shape[0]*3), Image.Resampling.NEAREST)
            sheet.paste(image, (x, row*110+8))
            x += image.width + 4
    sheet.save(args.output / "comparison.png")
    print(f"Evaluated {len(chars)} UI/stress glyphs, {len(report['faces'])} faces × {len(report['methods'])} methods (Micro: ASCII). Reports: {args.output}")


if __name__ == "__main__":
    main()
