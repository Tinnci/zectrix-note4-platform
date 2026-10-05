"""Catalog frequency, observed layout context, and explicit measurement inputs."""
import ast
from collections import Counter, defaultdict
import json
import math
import re

import numpy as np
from PIL import Image
from cjk_metrics import stats


def script(char):
    cp = ord(char)
    if 0x3400 <= cp <= 0x9fff:
        return "cjk"
    if char.isascii() and char.isalpha():
        return "latin"
    if char.isascii() and char.isdigit():
        return "digit"
    return "other"


def catalog_entries(path):
    literal = r'"(?:[^"\\]|\\.)*"'
    pattern = re.compile(r'^NOTE4_TEXT\((\w+),\s*('+literal+r'),\s*('+literal+r')\)\s*$')
    result = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.startswith("NOTE4_TEXT("):
            continue
        match = pattern.fullmatch(line)
        if not match:
            raise ValueError(f"Unsupported catalog syntax at {path}:{number}")
        for language, literal_text in zip(("en", "zh"), match.groups()[1:]):
            text = ast.literal_eval(literal_text)
            # Remove placeholders, preserving boundaries. '%%' is a literal '%'.
            sentinel = "\x00"
            text = text.replace("%%", sentinel)
            text = re.sub(r'%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn]', "\n", text)
            result.append({"id": match[1], "language": language, "text": text.replace(sentinel, "%")})
    return result


def pair_graph(documents):
    """Document presence and adjacency are separate denominators, not usage odds."""
    documents = list(documents)
    presence, pairs, adjacent, frequency = Counter(), Counter(), Counter(), Counter()
    for text in documents:
        frequency.update(c for c in text if not c.isspace())
        members = sorted(set(c for c in text if not c.isspace()))
        presence.update(members)
        for i, a in enumerate(members):
            for b in members[i+1:]:
                pairs[a+b] += 1
        for a, b in zip(text, text[1:]):
            if not a.isspace() and not b.isspace():
                adjacent[a+b] += 1
    count = len(documents)
    graph = []
    for pair, together in sorted(pairs.items()):
        pa, pb = (presence[c] for c in pair)
        lift = together*count/(pa*pb)
        graph.append({"pair": pair, "documents": together, "conditional_b_given_a": together/pa,
                      "conditional_a_given_b": together/pb, "jaccard": together/(pa+pb-together),
                      "lift": lift, "pmi_bits": math.log2(lift), "low_support": together < 5})
    return {"documents": count, "frequency": dict(frequency), "presence": dict(presence),
            "adjacency": dict(adjacent), "cooccurrence": graph}


def corpus(path):
    entries = catalog_entries(path)
    return {"source": str(path), "weight_basis": "Occurrences in source strings, NOT measured usage or dwell time",
            "entries": entries, "languages": {language: pair_graph(e["text"] for e in entries if e["language"] == language) for language in ("en", "zh")},
            "combined": pair_graph(e["text"] for e in entries)}


def load_embedded(path):
    """Decode the actual committed/generated 1bpp record format, including trims."""
    text = path.read_text()
    result = {}
    for role, height in re.findall(r'kEditorial(\w+)Height = (\d+);', text):
        data_match = re.search(r'kEditorial'+role+r'Data\[\] = \{(.*?)\};', text, re.S)
        records_match = re.search(r'kEditorial'+role+r'Glyphs\[\] = \{(.*?)\};', text, re.S)
        if not data_match or not records_match:
            raise ValueError(f"Incomplete embedded font {role}")
        data = np.array([int(n) for n in re.findall(r'\d+', data_match[1])], dtype=np.uint8)
        masks, offsets = {}, []
        for cp, offset, width, top, rows in re.findall(r'\{0x([0-9a-f]+), (\d+), (\d+), (\d+), (\d+)\}', records_match[1]):
            cp, offset, width, top, rows = int(cp, 16), int(offset), int(width), int(top), int(rows)
            if not width or top+rows > int(height) or offset+(width*rows+7)//8 > len(data):
                raise ValueError(f"Invalid embedded glyph {role} U+{cp:04X}")
            if chr(cp) in masks:
                raise ValueError(f"Duplicate embedded glyph {role} U+{cp:04X}")
            mask = np.zeros((int(height), width), bool)
            bits = np.unpackbits(data[offset:offset+(width*rows+7)//8])[:width*rows]
            mask[top:top+rows] = bits.reshape(rows, width)
            masks[chr(cp)] = mask
            offsets.append(offset)
        if offsets != sorted(offsets):
            raise ValueError(f"Unsorted offsets for {role}")
        result[role] = {"masks": masks, "bitmap_bytes": len(data), "index_bytes": len(masks)*12}
    if not result:
        raise ValueError("No editorial font records found")
    return result


def flatten(value, prefix=""):
    for key, item in value.items():
        name = f"{prefix}.{key}" if prefix else key
        if isinstance(item, dict):
            yield from flatten(item, name)
        elif isinstance(item, (int, float)) and not isinstance(item, bool):
            yield name, item
        elif item is None:
            yield name, np.nan
        elif isinstance(item, list) and "quadrant" in key:
            for index, number in enumerate(item):
                yield f"{name}.{index}", float(number)


def aggregate(glyphs, frequency):
    columns = defaultdict(list)
    chars = list(glyphs)
    for char in chars:
        for key, value in flatten(glyphs[char]):
            columns[key].append(value)
    # Lists (quadrants, curves) retain their independent per-glyph results.
    weights = [frequency.get(c, 0) for c in chars]
    return {"unweighted": {k: stats(v) for k, v in columns.items()},
            "catalog_frequency_weighted": {k: stats(v, weights) for k, v in columns.items()}}


def contextual_neighbors(neighbors, source, pages=None):
    """Join distances to exposure evidence without multiplying them into a score."""
    source_pairs = {p["pair"]: p for p in source["combined"]["cooccurrence"]}
    page_pairs = {p["pair"]: p for p in pages["same_page"]["cooccurrence"]} if pages else {}
    rows = []
    for char, result in neighbors.items():
        for neighbor in result["nearest"]:
            other = neighbor["char"]
            pair = "".join(sorted((char, other)))
            src, page = source_pairs.get(pair), page_pairs.get(pair)
            rows.append({"char": char, **neighbor,
                         "distance_retention": neighbor["distance"]/neighbor["reference_distance"] if neighbor["reference_distance"] else None,
                         "source_occurrences": [source["combined"]["frequency"].get(c, 0) for c in (char, other)],
                         "source_adjacent_forward": source["combined"]["adjacency"].get(char+other, 0),
                         # Graphs already contain conditional/Jaccard/PMI values;
                         # reference their ordinary pair keys instead of duplicating
                         # the same edge for every role and raster candidate.
                         "source_pair_key": pair if src else None,
                         "source_same_string_count": src["documents"] if src else 0,
                         "fixture_pair_key": pair if page else None,
                         "fixture_same_page_count": page["documents"] if page else (0 if pages else None),
                         "fixture_adjacent_forward": pages["adjacency"].get(char+other, 0) if pages else None})
    return rows


def extended_summaries(glyphs, source, role, pages=None):
    result = aggregate(glyphs, source["combined"]["frequency"])
    if pages:
        result["fixture_frequency_weighted_by_language"] = {
            f["language"]: aggregate(glyphs, f["frequency"])["catalog_frequency_weighted"]
            for f in pages["frequencies_by_font"]
            if f["role"] == role and f["source"] == "editorial" and f["style"] == 0}
    return result


def source_mask(event):
    """Reproduce immutable 16-row fallback/reader masks and SDK style sampling.

No inferred outline: these are the actual rows exported by the host painter.
This helper is independently pixel-checked against real framebuffer crops.
"""
    size, style = event["size"], event.get("style", 0)
    cp, source_width = event["cp"], event["source_width"]
    dense = source_width >= 16 or (0x2e80 <= cp <= 0x9fff or 0xac00 <= cp <= 0xd7ff or
        0xf900 <= cp <= 0xfaff or 0xff00 <= cp <= 0xffef or 0x20000 <= cp <= 0x323af)
    space = cp in (32, 0xa0, 0x3000) or 0x2000 <= cp <= 0x200a
    weight = size//16 if style & 1 and not dense and not space else 0
    slant = size//8 if style & 2 and not dense and not space else 0
    underline = bool(style & 8 or dense and style & 3)
    width = source_width*size//16
    result = np.zeros((event["height"], event["width"]), bool)
    top = 2 if event["source"] == "unicode_fallback" else 0
    rows = event["source_rows"]
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
    for y in range(result.shape[0]-top):
        bits = rows[y*16//size] if y < size else 0
        shift = slant*(size-1-y)//(size-1) if y < size else 0
        for x in range(result.shape[1]):
            ink = underline and y == size+1
            for delta in range(weight+1):
                src = x-shift-delta
                if 0 <= src < width and bits & (0x8000 >> (src*16//size)):
                    ink = True
            if style & 4 and bayer[(event["y"]+y+top) & 3, (event["x"]+x) & 3] >= 12:
                ink = False
            result[y+top, x] = ink
    return result


def page_context(directory, embedded):
    """Measured host paint placements, not presumed string-to-page associations."""
    from cjk_metrics import Features
    pages, documents, lines, words, adjacent, observations = [], [], [], [], [], defaultdict(list)
    font_frequencies = defaultdict(Counter)
    cache = {}
    shape_ids, glyph_shapes = {}, []
    for path in sorted(directory.glob("*.text.json")):
        trace = json.loads(path.read_text())
        bitmap_path = path.with_name(path.name.removesuffix(".text.json")+".pbm")
        with Image.open(bitmap_path) as image:
            pixels = ~np.asarray(image, dtype=bool)
        if pixels.shape != (trace["height"], trace["width"]):
            raise ValueError(f"Trace/bitmap dimensions differ: {path}")
        runs = defaultdict(list)
        visible, occluded = [], []
        latest = {}
        for event in trace["glyphs"]:
            # Repainting the same cell is not another user-visible occurrence.
            latest[(event["x"], event["y"], event["width"], event["height"])] = event
        for event in latest.values():
            c = chr(event["cp"])
            x, y, w, h = (event[k] for k in ("x", "y", "width", "height"))
            left, top, right, bottom = (event[k] for k in ("clip_left", "clip_top", "clip_right", "clip_bottom"))
            if right <= left or bottom <= top:
                continue
            roi = pixels[top:bottom, left:right]
            ink = ~roi if event["inverted"] else roi
            key = (event["source"], event["role"], c)
            mask = embedded.get(event["role"], {}).get("masks", {}).get(c) if event["source"] == "editorial" else None
            if mask is None and "source_rows" in event:
                mask = source_mask(event)
                phase = (x % 4, y % 4) if event.get("style", 0) & 4 else (0, 0)
                key += (event["size"], event.get("style", 0), tuple(event["source_rows"]), phase)
            retained = None
            if mask is not None:
                expected = mask[top-y:bottom-y, left-x:right-x]
                retained = float((expected & ink).sum())/int(expected.sum()) if expected.any() else None
                if key not in cache:
                    cache[key] = Features(mask).descriptors()
                    shape_ids[key] = len(glyph_shapes)
                    glyph_shapes.append({"id": shape_ids[key], "char": c, "role": event["role"],
                        "source": event["source"], "size": event["size"], "style": event.get("style", 0),
                        "source_width": event.get("source_width"), "screen_phase": [x % 4, y % 4] if event.get("style", 0) & 4 else None,
                        "descriptors": cache[key], "reference_status": "matching_outline_required" if event["source"] == "editorial" else "native_bitmap_only"})
                descriptor = cache[key]
                weight = descriptor["normalized_effective_width"]
                observed_role = f"{event['role']}:{event['source']}:{event['size']}:{event.get('style', 0)}"
                if retained != 0:
                    observations[(observed_role, script(c))].append(weight if weight is not None else np.nan)
            else:
                weight = None  # framebuffer crop includes decorations: do NOT call it pure glyph weight
                descriptor = None
            # Immutable rows remain in the input sidecar, not repeated again in
            # hundreds of thousands of report placements.
            record = {**{k: v for k, v in event.items() if k != "source_rows"},
                      "source_mask_available": mask is not None,
                      "font_shape_id": shape_ids.get(key),
                      "ink_left_bearing_px": descriptor["left_bearing_px"] if descriptor else None,
                      "ink_right_bearing_px": descriptor["right_bearing_px"] if descriptor else None,
                      "char": c, "script": script(c), "visible_roi_density": float(ink.mean()),
                      "visible_roi_area_px": ink.size,
                      "expected_ink_retained": retained, "normalized_effective_width": weight,
                      "clipped": left != x or top != y or right != x+w or bottom != y+h}
            if retained == 0:
                occluded.append(record)
                continue
            font_frequencies[(event["role"], event["source"], event["size"], event.get("style", 0), trace["language"])][c] += 1
            # Body glyphs paint individually; group by actual baseline/role, sort below.
            run_key = event["run"] or f"body-{y}-{event['role']}"
            runs[run_key].append(record)
            visible.append(record)
        text_runs, transitions = [], []
        for run_id, events in runs.items():
            events.sort(key=lambda e: e["x"])
            text = "".join(e["char"] for e in events)
            lines.append(text)
            words.extend(re.findall(r'[A-Za-z]+|[\u3400-\u9fff]+|[0-9]+', text))
            text_runs.append({"run": run_id, "text": text, "role": events[0]["role"],
                              "mixed_styles": len({e.get("style", 0) for e in events}) > 1,
                              "mean_character_roi_density": float(np.mean([e["visible_roi_density"] for e in events])),
                              "line_grayness": float(np.average([e["visible_roi_density"] for e in events], weights=[e["visible_roi_area_px"] for e in events])),
                              "same_role_weight_variation": stats([e["normalized_effective_width"] if e["normalized_effective_width"] is not None else np.nan for e in events])})
            for a, b in zip(events, events[1:]):
                if a["char"].isspace() or b["char"].isspace():
                    continue
                gap = b["x"]-(a["x"]+a["width"])
                wa, wb = a["normalized_effective_width"], b["normalized_effective_width"]
                change = abs(wa-wb) if wa is not None and wb is not None else None
                bearings = (a["ink_right_bearing_px"], b["ink_left_bearing_px"])
                ink_gap = gap+sum(bearings) if all(v is not None for v in bearings) else None
                left_font = [a["role"], a["source"], a["size"], a.get("style", 0)]
                right_font = [b["role"], b["source"], b["size"], b.get("style", 0)]
                transitions.append({"pair": a["char"]+b["char"], "role": a["role"], "cross_script": a["script"] != b["script"],
                                    "left_font": left_font, "right_font": right_font, "same_font_style": left_font == right_font,
                                    "weight_jump": change, "cell_gap_px": gap, "ink_gap_px": ink_gap,
                                    "crowding_spacing_proxy": ink_gap/max(1, min(a["height"], b["height"])) if ink_gap is not None else None})
        documents.append(" ".join(r["text"] for r in text_runs))
        adjacent.extend(t["pair"] for t in transitions)
        pages.append({"scene": path.name.removesuffix(".text.json"), "trace": path.name, "language": trace["language"],
                      "width": trace["width"], "height": trace["height"], "trace_dropped": trace["dropped"],
                      "frame_ink_fraction": float(pixels.mean()),
                      "glyphs": visible, "occluded_glyphs": occluded, "runs": text_runs, "transitions": transitions})
    cross_script = []
    for role in sorted({role for role, _ in observations}):
        cjk, latin = observations.get((role, "cjk"), []), observations.get((role, "latin"), [])
        sc, sl = stats(cjk), stats(latin)
        cross_script.append({"role": role, "cjk": sc, "latin": sl,
                             "mean_ratio_cjk_over_latin": sc["mean"]/sl["mean"] if sc["mean"] is not None and sl["mean"] else None,
                             "interpretation": "Descriptive complexity-sensitive ratio; target is NOT universally 1"})
    return {"status": "computed" if pages else "needs_input", "pages": pages,
            "glyph_shapes": glyph_shapes,
            "frequencies_by_font": [{"role": key[0], "source": key[1], "size": key[2], "style": key[3], "language": key[4], "frequency": dict(value)} for key, value in sorted(font_frequencies.items())],
            "same_page": pair_graph(documents), "same_text_run": pair_graph(lines), "same_word_or_cjk_run": pair_graph(words),
            "adjacency": dict(Counter(adjacent)), "cross_script_by_role": cross_script,
            "limitations": "Host fixtures have equal occurrence counts, NOT user exposure probabilities. Editorial masks come from the actual header; fallback/body masks come from exported immutable source rows with actual style sampling. Completely erased events are excluded, partially occluded events are flagged. CJK runs are not dictionary-segmented words. Crowding is a spacing proxy, not measured perception."}
