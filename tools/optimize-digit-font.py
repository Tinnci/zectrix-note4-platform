#!/usr/bin/env python3
"""Offline, uncalibrated e-paper font analysis; no hardware, flashing or quality gates."""
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=11,<13", "numpy>=2,<3", "scipy>=1.14,<2"]
# ///
import argparse
import html
import json
import math
import time
import zlib
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
from digit_font import ROOT, CODECS, source_glyphs, rasterize, trim, bits_of, choose, encode_codec, decode, tile_pack, tile_unpack, pack

HEIGHTS = (24, 32, 40, 48, 64)
# Hypothetical sensitivity conditions, NOT measured Note4 temperature/waveform mappings.
SCENARIOS = [
    {"name": "ideal", "spread_px": 0.0, "ghost": 0.0, "contrast": 1.0, "view_blur_px": 0.35},
    {"name": "soft", "spread_px": 0.35, "ghost": 0.0, "contrast": 0.8, "view_blur_px": 0.5},
    {"name": "partial-history", "spread_px": 0.35, "ghost": 0.10, "contrast": 0.8, "view_blur_px": 0.5},
    {"name": "stress", "spread_px": 0.65, "ghost": 0.20, "contrast": 0.6, "view_blur_px": 0.75},
]

def topology(mask):
    # Dual connectivity: 8-connected ink, 4-connected background.
    mask = np.pad(np.asarray(mask, dtype=bool), 1)
    components = ndi.label(mask, np.ones((3, 3)))[1]
    holes = ndi.label(~mask, ndi.generate_binary_structure(2, 1))[1] - 1
    return int(components), int(holes)

def boundary(mask):
    return mask & ~ndi.binary_erosion(mask)

def signed_distance(mask):
    return ndi.distance_transform_edt(mask) - ndi.distance_transform_edt(~mask)

def ssim(a, b, region):
    """Gaussian-window SSIM, averaged in glyph ROI instead of white page margins."""
    mu_a, mu_b = ndi.gaussian_filter(a, 1.5), ndi.gaussian_filter(b, 1.5)
    var_a = np.maximum(0, ndi.gaussian_filter(a * a, 1.5) - mu_a * mu_a)
    var_b = np.maximum(0, ndi.gaussian_filter(b * b, 1.5) - mu_b * mu_b)
    covariance = ndi.gaussian_filter(a * b, 1.5) - mu_a * mu_b
    score = ((2 * mu_a * mu_b + .01 ** 2) * (2 * covariance + .03 ** 2) /
             ((mu_a ** 2 + mu_b ** 2 + .01 ** 2) * (var_a + var_b + .03 ** 2)))
    return float(np.mean(score[region]))

def simulate(ink, scenario, history=None, inverted=False, rotation=0):
    """Linear reflectance/PSF/history sensitivity model, not a physical EPD solver."""
    black = 1 - ink if inverted else ink
    old = black if history is None else (1 - history if inverted else history)
    reflectance = 1 - ((1 - scenario["ghost"]) * black + scenario["ghost"] * old)
    if scenario["spread_px"]:
        reflectance = ndi.gaussian_filter(reflectance, scenario["spread_px"])
    reflectance = .5 + scenario["contrast"] * (reflectance - .5)
    if scenario["view_blur_px"]:
        reflectance = ndi.gaussian_filter(reflectance, scenario["view_blur_px"])
    return np.rot90(reflectance, rotation)

def reference_for(source, height):
    width = min(round(42 * height / 48), max(1, round(source.width * height / source.height)))
    coverage = 1 - np.asarray(source.resize((width, height), Image.Resampling.BOX), dtype=float) / 255
    high = np.asarray(source.resize((width * 4, height * 4), Image.Resampling.LANCZOS)) < 128
    high = np.pad(high, 8)
    edge = boundary(high)
    distance = signed_distance(high) / 4
    return {"coverage": np.pad(coverage, 2), "high": high, "edge": edge,
            "edge_distance": ndi.distance_transform_edt(~edge) / 4,
            "distance": distance, "topology": topology(high),
            "roi": ndi.binary_dilation(np.pad(coverage > .05, 2), iterations=2)}

def evaluate(image, reference):
    ink = np.asarray(image) == 0
    high = np.pad(np.repeat(np.repeat(ink, 4, axis=0), 4, axis=1), 8)
    edge = boundary(high)
    distances = np.concatenate((reference["edge_distance"][edge],
                                (ndi.distance_transform_edt(~edge) / 4)[reference["edge"]]))
    sdf_delta = signed_distance(high) / 4 - reference["distance"]
    edge_band = np.abs(reference["distance"]) <= 1
    # High-frequency signed-distance residual relative to this style's own shape.
    jagged = float(np.mean(np.abs(sdf_delta - ndi.gaussian_filter(sdf_delta, 2))[edge_band]))
    components, holes = topology(ink)
    topology_error = abs(components - reference["topology"][0]) + abs(holes - reference["topology"][1])
    low_reference = reference["coverage"][2:-2, 2:-2] >= .5
    # Width proxy: twice the foreground distance-transform medial ridge radius.
    def width_proxy(mask):
        distance = ndi.distance_transform_edt(np.pad(mask, 1))
        ridge = (distance > 0) & (distance >= ndi.maximum_filter(distance, size=3))
        return float(np.median(2 * distance[ridge])) if ridge.any() else 0.0
    stroke = abs(width_proxy(ink) - width_proxy(low_reference)) / max(1, width_proxy(low_reference))
    padded = np.pad(ink.astype(float), 2)
    history = np.roll(reference["coverage"], 2, axis=1)
    scenario_values = []
    for scenario in SCENARIOS:
        observed = simulate(padded, scenario, history)
        ideal = simulate(reference["coverage"], {**scenario, "ghost": 0}, None)
        scenario_values.append(1 - ssim(observed, ideal, reference["roi"]))
    # Explicit engineering weights, not a validated human readability model.
    edge95 = float(np.percentile(distances, 95))
    perceptual = float(np.mean(scenario_values))
    worst = max(scenario_values)
    distortion = edge95 + .5 * jagged + .25 * stroke + 2 * perceptual + worst
    return {"edge_mean_px": float(np.mean(distances)), "edge_p95_px": edge95,
            "jagged_residual_px": jagged, "stroke_proxy_relative_error": stroke,
            "components": components, "holes": holes, "topology_error": topology_error,
            "scenario_distortions": scenario_values, "perceptual_mean": perceptual,
            "perceptual_worst": worst, "distortion": distortion}

def pareto(records):
    valid = [r for r in records if r["quality"]["topology_error"] == 0]
    return [r for r in valid if not any(
        q["bytes"] <= r["bytes"] and q["quality"]["distortion"] <= r["quality"]["distortion"] and
        (q["bytes"] < r["bytes"] or q["quality"]["distortion"] < r["quality"]["distortion"])
        for q in valid)]

def byte_entropy(data):
    counts = np.bincount(np.frombuffer(data, dtype=np.uint8), minlength=256)
    p = counts[counts > 0] / max(1, len(data))
    return float(-np.sum(p * np.log2(p)))

def spatial_entropy(glyphs):
    counts = np.zeros((4, 2), dtype=np.int64)
    for glyph in glyphs:
        ink = (np.asarray(glyph) == 0).astype(np.uint8)
        north = np.zeros_like(ink); north[1:] = ink[:-1]
        west = np.zeros_like(ink); west[:, 1:] = ink[:, :-1]
        context = north * 2 + west
        counts += np.bincount((context * 2 + ink).ravel(), minlength=8).reshape(4, 2)
    total = int(counts.sum())
    def entropy(values):
        p = values[values > 0] / values.sum()
        return float(-np.sum(p * np.log2(p)))
    conditional = sum(int(row.sum()) / total * entropy(row) for row in counts if row.sum())
    return {"pixels": total, "marginal_bits_per_pixel": entropy(counts.sum(axis=0)),
            "north_west_conditional_bits_per_pixel": conditional,
            "note": "Empirical spatial redundancy diagnostic; excludes model cost, not a guaranteed attainable bit rate or sharpness score."}

def codec_report(glyphs, repeats=10):
    result = []
    total_pixels = sum(g.width * g.height for g in glyphs)
    for codec, name in enumerate(CODECS):
        encoded = [encode_codec(bits_of(g), g.width, codec) for g in glyphs]
        started = time.perf_counter_ns()
        for _ in range(repeats):
            decoded = [decode(p, g.width, g.height, codec) for p, g in zip(encoded, glyphs)]
        elapsed = (time.perf_counter_ns() - started) / repeats
        if any(actual != bits_of(g) for actual, g in zip(decoded, glyphs)):
            raise ValueError("Lossless mismatch")
        payload = b"".join(encoded)
        result.append({"codec": name, "payload_bytes": len(payload), "index_bytes": 6 * len(glyphs),
                       "data_total_bytes": len(payload) + 6 * len(glyphs),
                       "python_decode_ns_per_pixel": elapsed / total_pixels,
                       "byte_entropy_bits": byte_entropy(payload),
                       "firmware_decoder": True, "scratch_bytes": 6 if codec == 2 else 0})
    for codecs, name in (((0, 1), "mixed_raw_rle"), ((0, 1, 2), "mixed_raw_rle_xor"),
                         ((0, 1, 3), "mixed_raw_row_column"), ((0, 1, 2, 3), "mixed_all_streaming")):
        encoded = [choose(bits_of(g), g.width, codecs)[:2] for g in glyphs]
        started = time.perf_counter_ns()
        for _ in range(repeats):
            decoded = [decode(p, g.width, g.height, codec) for (p, codec), g in zip(encoded, glyphs)]
        elapsed = (time.perf_counter_ns() - started) / repeats
        if any(actual != bits_of(g) for actual, g in zip(decoded, glyphs)):
            raise ValueError("Mixed lossless mismatch")
        payload = b"".join(p for p, _ in encoded)
        result.append({"codec": name, "payload_bytes": len(payload), "index_bytes": 6 * len(glyphs),
                       "data_total_bytes": len(payload) + 6 * len(glyphs), "firmware_decoder": True,
                       "python_decode_ns_per_pixel": elapsed / total_pixels,
                       "scratch_bytes": 6 if 2 in codecs else 0,
                       "glyph_codec_counts": {CODECS[c]: sum(codec == c for _, codec in encoded) for c in codecs}})
    for size in (4, 8):
        dictionary, indices, shapes = tile_pack(glyphs, size)
        started = time.perf_counter_ns()
        for _ in range(repeats):
            restored = tile_unpack(dictionary, indices, shapes, size)
        elapsed = (time.perf_counter_ns() - started) / repeats
        if any(bits_of(a) != bits_of(b) for a, b in zip(glyphs, restored)):
            raise ValueError("Tile mismatch")
        dictionary_bytes = sum(map(len, dictionary))
        payload_bytes = dictionary_bytes + len(indices) * 2
        result.append({"codec": f"tiles{size}", "payload_bytes": payload_bytes,
                       "dictionary_bytes": dictionary_bytes, "index_bytes": 6 * len(glyphs),
                       "data_total_bytes": payload_bytes + 6 * len(glyphs),
                       "python_decode_ns_per_pixel": elapsed / total_pixels, "firmware_decoder": False,
                       "scratch_bytes": (size * size + 7) // 8})
    raw = b"".join(pack(bits_of(g)) for g in glyphs)
    compressed = zlib.compress(raw, 9)
    started = time.perf_counter_ns()
    for _ in range(repeats):
        restored = zlib.decompress(compressed)
    elapsed = (time.perf_counter_ns() - started) / repeats
    if restored != raw:
        raise ValueError("Deflate mismatch")
    result.append({"codec": "whole_font_deflate", "payload_bytes": len(compressed),
                   "index_bytes": 6 * len(glyphs), "data_total_bytes": len(compressed) + 6 * len(glyphs),
                   "python_decode_ns_per_pixel": elapsed / total_pixels,
                   "firmware_decoder": False, "output_buffer_bytes": len(raw),
                   "note": "Shared stream: no independent glyph access; decoder code/workspace not included."})
    return result

def date_ink(glyphs, value):
    digits = (glyphs[value // 10], glyphs[value % 10])
    canvas = Image.new("1", (90, 56), 1)
    x = (90 - digits[0].width - 3 - digits[1].width) // 2
    for glyph in digits:
        canvas.paste(glyph, (x, 4))
        x += glyph.width + 3
    return (np.asarray(canvas) == 0).astype(float)

def transition_report(glyphs):
    result = []
    for style in range(5):
        family = glyphs[style * 10:style * 10 + 10]
        for old, new in ((9, 10), (19, 20), (30, 31)):
            history, current = date_ink(family, old), date_ink(family, new)
            roi = ndi.binary_dilation((current + history) > 0, iterations=2)
            for scenario in SCENARIOS:
                for inverted in (False, True):
                    for rotation in range(4):
                        observed = simulate(current, scenario, history, inverted, rotation)
                        ideal = simulate(current, {**scenario, "ghost": 0}, None, inverted, rotation)
                        result.append({"style": style, "old": old, "new": new,
                                       "scenario": scenario["name"], "inverted": inverted, "rotation_deg": rotation * 90,
                                       "ssim_distortion": 1 - ssim(observed, ideal, np.rot90(roi, rotation))})
    return result

def sharpness_report():
    # Synthetic straight-edge ESF, NOT an ISO-compliant screen/camera MTF measurement.
    ink = np.zeros((64, 128), dtype=float)
    ink[:, 64:] = 1
    result = []
    for scenario in SCENARIOS:
        esf = simulate(ink, {**scenario, "ghost": 0})[32]
        normalized = (esf[0] - esf) / (esf[0] - esf[-1])
        x = np.arange(len(esf))
        width = float(np.interp(.9, normalized, x) - np.interp(.1, normalized, x))
        result.append({"scenario": scenario["name"], "edge_10_90_width_px": width,
                       "reflectance_contrast": float(esf[0] - esf[-1])})
    return result

def confusion_report(sources, selections):
    """Appearance separation only; never claim OCR or human recognition accuracy."""
    result = []
    parameters = {(s["height"], s["style"], s["digit"]): s for s in selections}
    for height in sorted({s["height"] for s in selections}):
        for style in range(5):
            rasters = []
            for digit in range(10):
                selected = parameters[height, style, digit]
                glyph = trim(rasterize(sources[style * 10 + digit], height, selected["threshold"], selected["method"]))
                canvas = Image.new("1", (height + 8, height + 8), 1)
                canvas.paste(glyph, ((canvas.width - glyph.width) // 2, 4))
                rasters.append((np.asarray(canvas) == 0).astype(float))
            for scenario in SCENARIOS:
                observations = [simulate(r, {**scenario, "ghost": 0}) for r in rasters]
                pairs = []
                for a in range(10):
                    for b in range(a + 1, 10):
                        roi = ndi.binary_dilation((rasters[a] + rasters[b]) > 0, iterations=2)
                        separation = float(np.sqrt(np.mean((observations[a][roi] - observations[b][roi]) ** 2)))
                        pairs.append({"digits": [a, b], "reflectance_rmse_separation": separation})
                result.append({"height": height, "style": style, "scenario": scenario["name"],
                               "closest_pairs": sorted(pairs, key=lambda p: p["reflectance_rmse_separation"])[:5]})
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build-font-analysis")
    parser.add_argument("--heights", nargs="+", type=int, default=list(HEIGHTS))
    parser.add_argument("--ppi", nargs="+", type=float, default=[150, 200, 300])
    parser.add_argument("--viewing-distance-mm", nargs="+", type=float, default=[250, 400])
    parser.add_argument("--rate-weight", type=float, default=.001, help="Distortion units per encoded byte; engineering operating point")
    parser.add_argument("--codecs", nargs="+", choices=CODECS, default=list(CODECS))
    parser.add_argument("--measurements", type=Path, help="Explicitly reuse geometry measurements from unchanged source artwork/metric implementation; rasterization and codecs still execute")
    args = parser.parse_args()
    if any(h < 8 or h > 128 for h in args.heights):
        parser.error("Heights must be 8..128")
    if len(set(args.heights)) != len(args.heights):
        parser.error("Heights must be unique")
    if any(not math.isfinite(n) or n <= 0 for n in args.ppi + args.viewing_distance_mm):
        parser.error("PPI and viewing distance must be finite and positive")
    if not math.isfinite(args.rate_weight) or args.rate_weight < 0:
        parser.error("Rate weight must be finite and nonnegative")
    args.output.mkdir(parents=True, exist_ok=True)
    sources = source_glyphs()
    reusable = {}
    if args.measurements:
        previous = json.loads(args.measurements.read_text())
        reusable = {r["id"]: r["quality"] for r in previous["candidates"]}
    codecs = tuple(CODECS.index(name) for name in args.codecs)
    records, selections, native_before, native_after = [], [], [], []
    parameters = [("lanczos", t) for t in (128, 140, 150, 160, 172)] + [("area", t) for t in (128, 140, 150, 160, 172)]
    for height in args.heights:
        for index, source in enumerate(sources):
            reference = reference_for(source, height)
            candidates, rasters = [], {}
            for method, threshold in parameters:
                image = rasterize(source, height, threshold, method)
                key = f"{'abcde'[index // 10]}-{index % 10}-{height}-{method}-{threshold}"
                quality = reusable[key] if key in reusable else evaluate(image, reference)
                cropped = trim(image)
                payload, codec, raw_size = choose(bits_of(cropped), cropped.width, codecs)
                entry = {"id": key, "style": index // 10, "digit": index % 10, "height": height,
                         "method": method, "threshold": threshold, "quality": quality,
                         "bytes": len(payload) + 6, "raw_bytes": raw_size, "codec": CODECS[codec]}
                candidates.append(entry)
                rasters[key] = cropped
            original = next(r for r in candidates if r["method"] == "lanczos" and r["threshold"] == 150)
            front = pareto(candidates)
            # Rate-distortion operating point, with shape constraints and no quality regression.
            eligible = [r for r in front if r["quality"]["distortion"] <= original["quality"]["distortion"] and
                        r["quality"]["edge_p95_px"] <= original["quality"]["edge_p95_px"]]
            winner = min(eligible, key=lambda r: (r["quality"]["distortion"] + args.rate_weight * r["bytes"], r["bytes"])) if eligible else original
            improved = winner["quality"]["distortion"] < original["quality"]["distortion"] - 1e-9
            acceptable = winner["quality"]["topology_error"] == 0
            # Preserve original if no topology-safe measurable improvement exists.
            selected = winner if acceptable and improved else original
            selections.append({"style": index // 10, "digit": index % 10, "height": height,
                               "original": original["id"], "selected": selected["id"],
                               "method": selected["method"], "threshold": selected["threshold"],
                               "quality_delta": selected["quality"]["distortion"] - original["quality"]["distortion"],
                               "topology_safe_candidate_exists": bool(front),
                               "pareto": [r["id"] for r in front]})
            for r in candidates:
                r["pareto"] = r["id"] in {q["id"] for q in front}
            records.extend(candidates)
            if height == 48:
                native_before.append(rasters[original["id"]])
                native_after.append(rasters[selected["id"]])
        print(f"Analyzed {height}px: 50 glyphs × {len(parameters)} raster candidates", flush=True)
    if not native_after:
        native_before = [trim(rasterize(s)) for s in sources]
        native_after = native_before
    comparisons = codec_report(native_after)
    sheet = Image.new("1", (1000, 420), 1)
    painter = ImageDraw.Draw(sheet)
    for i, (before, after) in enumerate(zip(native_before, native_after)):
        row, col = i // 10, i % 10
        for offset, glyph in ((0, before), (500, after)):
            sheet.paste(glyph, (offset + col * 50 + (50 - glyph.width) // 2, row * 84 + 18))
        painter.text((3, row * 84 + 2), f"{'ABCDE'[row]} before", fill=0)
        painter.text((503, row * 84 + 2), f"{'ABCDE'[row]} candidate", fill=0)
    sheet.save(args.output / "native-comparison.png")
    # Illustrate same target + different old-page residual, polarity and rotation.
    example = np.pad(np.asarray(native_after[31]) == 0, 4).astype(float)
    histories = [np.zeros_like(example), np.ones_like(example), np.roll(example, 3, axis=1)]
    samples = Image.new("L", (720, 280), 255)
    for row, scenario in enumerate(SCENARIOS):
        for col, history in enumerate(histories):
            for inverted in (False, True):
                raster = simulate(example, scenario, history, inverted)
                raster = Image.fromarray(np.uint8(np.clip(raster, 0, 1) * 255))
                samples.paste(raster, (col * 240 + int(inverted) * 80, row * 70))
    samples.save(args.output / "simulated-conditions.png")
    report = {"model": "uncalibrated linear reflectance/PSF/history sensitivity model",
              "limitations": ["Not a Note4 physical waveform or temperature calibration", "SSIM/score is not human readability proof",
                              "Python timing is not MCU energy or latency", "Codec byte sizes exclude new decoder machine code unless measured separately",
                              "Source AI raster is a reference shape, not ground-truth type design"],
              "scenarios": SCENARIOS, "heights": args.heights, "streaming_codecs": args.codecs,
              "measurement_reuse_source": str(args.measurements) if args.measurements else None,
              "weights": {"edge95": 1, "jagged": .5, "stroke": .25, "ssim_mean": 2, "ssim_worst": 1, "rate_per_byte": args.rate_weight},
              "selections": selections, "candidates": records, "codecs": comparisons,
              "date_transitions": transition_report(native_after), "synthetic_sharpness": sharpness_report(),
              "appearance_separation": confusion_report(sources, selections), "spatial_entropy": spatial_entropy(native_after),
              "angular_sampling": [{"ppi": ppi, "viewing_distance_mm": distance,
                                    "pixel_pitch_mm": 25.4 / ppi,
                                    "pixel_arcmin": 2 * math.atan(25.4 / ppi / distance / 2) * 180 / math.pi * 60}
                                   for ppi in args.ppi for distance in args.viewing_distance_mm]}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    # This is a chosen build input, not a frozen image contract or quality gate.
    recipe = {"height": 48, "codecs": args.codecs, "glyphs": [{k: s[k] for k in ("style", "digit", "method", "threshold")}
                                        for s in selections if s["height"] == 48]}
    if recipe["glyphs"]:
        (args.output / "recipe.json").write_text(json.dumps(recipe, indent=2) + "\n")
    rows = "".join(f"<tr><td>{html.escape(r['codec'])}</td><td>{r['data_total_bytes']}</td><td>{r['python_decode_ns_per_pixel']:.1f}</td><td>{r['firmware_decoder']}</td></tr>" for r in comparisons)
    selected_rows = "".join(f"<tr><td>{'ABCDE'[s['style']]}/{s['digit']}</td><td>{s['height']}</td><td>{s['method']}/{s['threshold']}</td><td>{s['quality_delta']:.4f}</td><td>{s['topology_safe_candidate_exists']}</td></tr>" for s in selections)
    page = f"""<!doctype html><meta charset="utf-8"><title>E-paper font analysis</title>
<style>body{{font:16px system-ui;max-width:1100px;margin:24px auto}}img{{max-width:100%;image-rendering:pixelated}}td,th{{padding:6px;border:1px solid #ccc}}table{{border-collapse:collapse}}</style>
<h1>字体离线分析</h1><p>未校准仿真；不能证明 Note4 实机锐度、温度表现或人类辨识率。像素单位须结合实际 PPI/观看距离解释。几何指标复用来源：{html.escape(str(args.measurements))}；复用只适用于源图及指标实现未变。</p>
<h2>原生 48px：左为当前，右为候选</h2><img src="native-comparison.png">
<h2>敏感性仿真</h2><p>行：理想/扩散/局刷残影/压力。列组：旧全白/旧全黑/偏移旧字形；每组正色与反色。非实机照片。</p><img src="simulated-conditions.png">
<h2>无损编码</h2><p>包大小含索引/共享字典，不含新增解码机器码。Python 时间不能当作 ESP32-S3 时间或能耗。</p>
<table><tr><th>编码</th><th>数据包字节</th><th>Python ns/pixel</th><th>流式固件候选</th></tr>{rows}</table>
<h2>候选选择</h2><p>先保持连通分量/孔洞，再最小化显式工程失真函数；无安全改善时保留原图。负数为模型下改善，不代表主观审美提升。</p>
<label>字形 <select id="glyph"></select></label><svg id="plot" width="640" height="300" role="img" aria-label="率失真候选散点图"></svg>
<p>蓝色为 Pareto 候选，红圈为选择，黑色为原参数，灰色为其他候选。鼠标停留可查看参数。横轴为编码字节，纵轴为工程失真。</p>
<p>率失真工作点：D + {args.rate_weight} × 字节数；仅从无拓扑变化、轮廓 p95 不退化、综合失真不退化的候选中选取。不是自动质量门禁。</p>
<table><tr><th>字形</th><th>高度</th><th>参数</th><th>失真变化</th><th>存在拓扑安全候选</th></tr>{selected_rows}</table>
<p><a href="report.json">所有指标、情形、Pareto 候选 JSON</a> · <a href="recipe.json">48px 生成参数</a></p>"""
    (args.output / "index.html").write_text(page)
    script = """<script>
const records=RECORDS,selections=SELECTIONS,menu=document.querySelector('#glyph'),plot=document.querySelector('#plot'),ns='http://www.w3.org/2000/svg';
for(const s of selections){const o=document.createElement('option');o.value=s.selected;o.textContent='ABCDE'[s.style]+'/'+s.digit+' '+s.height+'px';menu.append(o);}
function element(name,attrs,text){const e=document.createElementNS(ns,name);for(const [k,v] of Object.entries(attrs))e.setAttribute(k,v);if(text!==undefined)e.textContent=text;plot.append(e);return e;}
function draw(){plot.replaceChildren();const s=selections.find(s=>s.selected===menu.value),r=records.filter(r=>r.style===s.style&&r.digit===s.digit&&r.height===s.height);const minX=Math.min(...r.map(r=>r.bytes))-2,maxX=Math.max(...r.map(r=>r.bytes))+2,maxY=Math.max(...r.map(r=>r.quality.distortion))*1.1;const x=v=>60+(v-minX)/(maxX-minX)*540,y=v=>250-v/maxY*220;
element('path',{d:'M60 20V250H600',fill:'none',stroke:'black'});element('text',{x:65,y:285},minX+' bytes');element('text',{x:530,y:285},maxX+' bytes');element('text',{x:5,y:25},maxY.toFixed(2));element('text',{x:35,y:252},'0');
for(const q of r){const dot=element('circle',{cx:x(q.bytes),cy:y(q.quality.distortion),r:q.id===s.selected?7:4,fill:q.id===s.original?'black':q.pareto?'#2676d8':'#888',stroke:q.id===s.selected?'#c22':'none','stroke-width':2});const t=document.createElementNS(ns,'title');t.textContent=q.method+'/'+q.threshold+' bytes='+q.bytes+' D='+q.quality.distortion.toFixed(4)+' topology error='+q.quality.topology_error;dot.append(t);}}
menu.addEventListener('change',draw);draw();</script>""".replace("RECORDS", json.dumps(records)).replace("SELECTIONS", json.dumps(selections))
    with (args.output / "index.html").open("a") as document:
        document.write(script)
    print(json.dumps(comparisons, ensure_ascii=False, indent=2))
    print(f"Report: {args.output / 'index.html'}", flush=True)

if __name__ == "__main__":
    main()
