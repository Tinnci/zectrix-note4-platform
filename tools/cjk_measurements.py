"""Optional real-data analysis. Never infer panel power or people from pixels."""
import math
import time
import zlib
import tracemalloc

import numpy as np
from scipy.optimize import minimize
from skimage.metrics import structural_similarity
from PIL import Image
from pathlib import Path
from cjk_metrics import stats


def positive(value, name, allow_zero=True):
    value = float(value)
    if not math.isfinite(value) or value < 0 or (not allow_zero and value == 0):
        raise ValueError(f"Invalid {name}: expected finite {'nonnegative' if allow_zero else 'positive'} value")
    return value


def compression(masks):
    """Actual lossless codec sizes including the current per-glyph index.

Decode time is host wall time only. zlib/RLE are experiments, NOT firmware
codec implementations; workspace/latency on ESP32 remain unmeasured.
"""
    raw, trimmed = bytearray(), bytearray()
    for mask in masks.values():
        raw.extend(np.packbits(mask.ravel()).tobytes())
        rows = np.flatnonzero(mask.any(axis=1))
        if rows.size:
            trimmed.extend(np.packbits(mask[rows[0]:rows[-1]+1].ravel()).tobytes())
    packed = bytes(trimmed)
    rle = bytearray()
    i = 0
    while i < len(packed):
        n = 1
        while i+n < len(packed) and packed[i+n] == packed[i] and n < 255:
            n += 1
        rle.extend((n, packed[i]))
        i += n
    def decode_rle():
        return b"".join(bytes([rle[i+1]])*rle[i] for i in range(0, len(rle), 2))
    compressed = zlib.compress(packed, 9)
    byte_values = np.frombuffer(packed, dtype=np.uint8)
    counts = np.bincount(byte_values, minlength=256)
    probabilities = counts[counts > 0]/max(1, len(byte_values))
    byte_entropy = float(-np.sum(probabilities*np.log2(probabilities)))
    conditional = 0.
    if len(byte_values) > 1:
        joint = np.bincount(byte_values[:-1].astype(int)*256+byte_values[1:], minlength=65536).reshape(256, 256)
        for row in joint:
            if row.sum():
                p = row[row > 0]/row.sum()
                conditional += row.sum()/(len(byte_values)-1)*float(-np.sum(p*np.log2(p)))
    cases = [("row_trim_1bpp", packed, lambda: bytes(bytearray(packed))),
             ("byte_rle", bytes(rle), decode_rle), ("zlib9", compressed, lambda: zlib.decompress(compressed))]
    result = []
    for name, data, decode in cases:
        assert decode() == packed
        # Warm up; keep timings separate from deterministic image metrics.
        decode()
        durations = []
        for _ in range(15):
            started = time.perf_counter_ns()
            decoded = decode()
            durations.append((time.perf_counter_ns()-started)/1000)
            assert decoded == packed
        tracemalloc.start()
        decode()
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        result.append({"codec": name, "bitmap_bytes": len(data), "current_index_bytes": 12*len(masks),
                       "bytes_with_current_index": len(data)+12*len(masks), "host_decode_us": stats(durations),
                       "decoded_bitmap_bytes": len(packed), "lossless": True,
                       "host_decode_peak_traced_bytes": peak,
                       "random_access": name == "row_trim_1bpp",
                       "firmware_cost_status": "needs_device_measurement"})
    return {"untrimmed_bitmap_bytes": len(raw), "row_trim_saved_bytes": len(raw)-len(packed), "codecs": result,
            "empirical_byte_entropy_bits": byte_entropy, "empirical_next_byte_conditional_entropy_bits": conditional,
            "entropy_note": "Empirical histograms include byte padding/glyph boundaries. Not a source entropy bound or attainable firmware size; metadata, finite samples and long-range structure matter.",
            "note": "Whole-face zlib/RLE require different indexing or full decode; current 12-byte index is counted for comparison, not a workable random-access codec design. Host timing includes Python allocation and is not device latency."}


def mutual_information(rows):
    joint = {}
    expected, answers = {}, {}
    for row in rows:
        pair = (row["expected"], row["answer"])
        joint[pair] = joint.get(pair, 0)+1
        expected[pair[0]] = expected.get(pair[0], 0)+1
        answers[pair[1]] = answers.get(pair[1], 0)+1
    n = len(rows)
    mi = sum(count/n*math.log2(count*n/(expected[a]*answers[b])) for (a, b), count in joint.items()) if n else None
    return {"mi_bits_empirical": mi, "confusion_matrix": [{"expected": a, "answer": b, "count": count} for (a, b), count in sorted(joint.items())],
            "note": "Finite-sample plug-in MI is biased; compare matched stimuli/exposure, not different alphabets as a single ranking."}


def human(rows):
    groups = {}
    for row in rows:
        if not all(isinstance(row.get(k), str) and row[k] for k in ("participant", "candidate", "role", "expected", "answer")):
            raise ValueError("Recognition rows require participant/candidate/role/expected/answer strings")
        positive(row["time_ms"], "time_ms")
        groups.setdefault((row["candidate"], row["role"]), []).append(row)
    result = []
    rng = np.random.default_rng(0)
    for (candidate, role), trials in groups.items():
        people = sorted({r["participant"] for r in trials})
        by_person = [[r for r in trials if r["participant"] == p] for p in people]
        accuracy = np.mean([r["expected"] == r["answer"] for r in trials])
        bootstrap, bootstrap_time, bootstrap_mi = [], [], []
        if len(people) >= 2:
            for _ in range(1000):
                selected = [r for i in rng.integers(0, len(people), len(people)) for r in by_person[i]]
                bootstrap.append(np.mean([r["expected"] == r["answer"] for r in selected]))
                bootstrap_time.append(np.mean([r["time_ms"] for r in selected]))
                bootstrap_mi.append(mutual_information(selected)["mi_bits_empirical"])
        result.append({"candidate": candidate, "role": role, "trials": len(trials), "participants": len(people),
                       "accuracy": float(accuracy), "accuracy_cluster_bootstrap_ci95": np.percentile(bootstrap, [2.5, 97.5]).tolist() if bootstrap else None,
                       "mean_time_cluster_bootstrap_ci95_ms": np.percentile(bootstrap_time, [2.5, 97.5]).tolist() if bootstrap_time else None,
                       "mi_cluster_bootstrap_ci95_bits": np.percentile(bootstrap_mi, [2.5, 97.5]).tolist() if bootstrap_mi else None,
                       "time_ms_all": stats([r["time_ms"] for r in trials]),
                       "time_ms_correct": stats([r["time_ms"] for r in trials if r["expected"] == r["answer"]]),
                       **mutual_information(trials)})
    return {"status": "measured" if rows else "needs_measurement", "groups": result,
            "limitations": "Confidence intervals resample participants, not supposedly independent characters. One participant has no population CI. Observational candidate differences are not causal; randomize matched role/orientation/stimulus blocks."}


def pse(rows):
    """Weight judgments are distinct from recognition. Logistic 50% crossover."""
    groups = {}
    for row in rows:
        if not isinstance(row.get("participant"), str) or not row["participant"]:
            raise ValueError("Weight judgment requires participant")
        if type(row.get("test_heavier")) is not bool:
            raise ValueError("test_heavier must be a boolean judgment")
        x = float(row["weight_delta"])
        if not math.isfinite(x):
            raise ValueError("Nonfinite weight delta")
        groups.setdefault((row["candidate"], row["role"]), []).append(row)
    result = []
    for (candidate, role), trials in groups.items():
        x = np.array([r["weight_delta"] for r in trials])
        y = np.array([r["test_heavier"] for r in trials], float)
        # Fit in dimensionless coordinates; arbitrary delta units must not
        # turn a legitimate steep slope into 'insufficient data'.
        center, scale = float(x.mean()), float(x.std())
        normalized = (x-center)/scale if scale else np.zeros_like(x)
        design = np.column_stack((np.ones_like(x), normalized))
        fit = minimize(lambda b: float(np.sum(np.logaddexp(0, design@b)-y*(design@b))), [0., 1.], method="BFGS")
        probability = 1/(1+np.exp(-np.clip(design@fit.x, -700, 700)))
        fisher = design.T @ ((probability*(1-probability))[:, None]*design)
        identifiable = bool(fit.success and scale > 0 and len(set(x)) >= 3 and 0 < y.sum() < len(y)
                            and abs(fit.x[1]) > 1e-6 and np.linalg.cond(fisher) < 1e8)
        crossover = float(center-fit.x[0]/fit.x[1]*scale) if identifiable else None
        bracketed = crossover is not None and float(x.min()) <= crossover <= float(x.max())
        result.append({"candidate": candidate, "role": role, "trials": len(trials),
                       "pse_weight_delta": crossover if bracketed else None, "slope": float(fit.x[1]/scale) if identifiable else None,
                       "status": "measured_fit" if bracketed else "insufficient_or_unbracketed_data"})
    return {"status": "measured" if rows else "needs_measurement", "groups": result,
            "limitations": "Descriptive logistic fit, not a calibrated visual-weight predictor. Delta units must be held constant across trials. Fit must bracket the 50% crossover; no extrapolated PSE is reported."}


def energy(rows):
    result = []
    for row in rows:
        samples = row["samples"]
        if len(samples) < 2:
            raise ValueError("Energy requires at least two measured voltage/current samples")
        t = np.array([positive(s["time_s"], "time_s") for s in samples])
        if not np.all(np.diff(t) > 0):
            raise ValueError("Energy timestamps must be strictly increasing")
        watts = np.array([positive(s["voltage_v"], "voltage_v")*positive(s["current_a"], "current_a") for s in samples])
        result.append({"id": row["id"], "energy_j": float(np.trapezoid(watts, t)), "duration_s": float(t[-1]-t[0]),
                       "peak_power_w": float(watts.max()), "samples": len(t), "measurement_boundary": row["measurement_boundary"]})
    return {"status": "measured" if rows else "needs_measurement", "runs": result}


def optical(rows, base):
    result = []
    for row in rows:
        black, white = float(row["black_level"]), float(row["white_level"])
        if not all(math.isfinite(v) for v in (black, white)) or not 0 <= black < white <= 255:
            raise ValueError("Optical calibration requires 0 <= black_level < white_level <= 255")
        with Image.open(base / row["capture"]) as image:
            captured = np.asarray(image.convert("L"), float)
        with Image.open(base / row["target"]) as image:
            target = 1-np.asarray(image.convert("L"), float)/255
        if captured.shape != target.shape:
            raise ValueError("Register capture and target to equal native pixel dimensions before evaluation")
        ink = np.clip((white-captured)/(white-black), 0, 1)
        mse = float(np.mean((ink-target)**2))
        extent = min(ink.shape)
        n = min(7, extent if extent % 2 else extent-1)
        measured = {"id": row["id"], "coverage_mse": mse,
                    "ssim": float(structural_similarity(ink, target, data_range=1, win_size=n)) if n >= 3 else None,
                    "calibration": {"black_level": black, "white_level": white},
                    "native_pixel_shape": list(ink.shape), "registration": row["registration"]}
        if "edge_roi" in row:
            x, y, w, h = row["edge_roi"]
            if any(type(v) is not int for v in (x, y, w, h)) or min(x, y) < 0 or min(w, h) <= 1 or x+w > ink.shape[1] or y+h > ink.shape[0]:
                raise ValueError("Invalid optical edge ROI")
            esf = ink[y:y+h, x:x+w].mean(axis=0)
            lsf = np.diff(esf)
            spectrum = np.abs(np.fft.rfft(lsf*np.hanning(len(lsf))))
            mtf = spectrum/spectrum[0] if spectrum[0] > 1e-9 else None
            frequency = np.fft.rfftfreq(len(lsf))
            crossing = np.flatnonzero(mtf <= .5) if mtf is not None else []
            f50 = None
            if len(crossing) and crossing[0] > 0:
                i = crossing[0]
                f50 = float(np.interp(.5, mtf[i-1:i+1][::-1], frequency[i-1:i+1][::-1]))
            measured["edge"] = {"esf": esf.tolist(), "lsf": lsf.tolist(), "mtf": mtf.tolist() if mtf is not None else None,
                                "frequency_cycles_per_native_pixel": frequency.tolist(), "mtf50": f50,
                                "edge_gradient_peak": float(np.max(np.abs(lsf))),
                                "note": "Axis-aligned native-pixel ESF proxy; camera blur/alignment and panel blur are not separated"}
        if "previous" in row:
            with Image.open(base / row["previous"]) as image:
                previous = 1-np.asarray(image.convert("L"), float)/255
            if previous.shape != target.shape:
                raise ValueError("Previous bitmap has different registration/dimensions")
            residue, changed = previous-target, np.abs(previous-target) > .5
            measured["ghost_residue_projection"] = float(np.sum((ink-target)[changed]*residue[changed])/np.sum(residue[changed]**2)) if changed.any() else None
            measured["changed_pixel_error"] = float(np.mean(np.abs(ink-target)[changed])) if changed.any() else None
        result.append(measured)
    return {"status": "measured" if rows else "needs_measurement", "captures": result,
            "note": "User supplies registered native-resolution captures and measured black/white calibration. Gaussian bitmap blur is not a panel measurement."}


def measurements(data=None, base=Path(".")):
    data = data or {}
    refreshes = data.get("refreshes", [])
    for row in refreshes:
        for key in ("busy_us", "spi_bytes", "ram_bytes", "waveform_triggers"):
            if key in row:
                positive(row[key], key)
    return {"recognition": human(data.get("recognition", [])), "weight_pse": pse(data.get("weight_judgments", [])),
            "energy": energy(data.get("energy", [])),
            "refresh": {"status": "measured" if refreshes else "needs_measurement", "records": refreshes,
                        "note": "BUSY time/byte counts are not energy without calibrated power data."},
            "optical": optical(data.get("optical", []), base)}


class DistsModel:
    """Optional trusted, locally exported TorchScript DISTS (no model downloads).

Export the author's pretrained model with its trained VGG/alpha/beta weights;
input is two [N,3,H,W] float tensors, output one distance per pair. Loading a
model executes code: use only a model you trust. No silent random-weight model.
"""
    def __init__(self, path):
        import torch
        self.torch = torch
        self.model = torch.jit.load(str(path), map_location="cpu").eval()

    def distance(self, image, reference):
        torch = self.torch
        height, width = image.shape
        pad = ((8, max(8, 32-height-8)), (8, max(8, 32-width-8)))
        # White surround, native scale; minimum CNN extent is padding, not resizing.
        tensors = [torch.from_numpy(np.pad(1-np.asarray(a, np.float32), pad, constant_values=1)).expand(1, 3, -1, -1).contiguous() for a in (image, reference)]
        with torch.inference_mode():
            output = self.model(*tensors)
        value = float(output.reshape(-1)[0])
        if output.numel() != 1 or not math.isfinite(value):
            raise ValueError("DISTS model must return one finite distance")
        return value
