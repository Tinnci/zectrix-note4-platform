"""Independent host-only glyph descriptors. None is a readability score.

All distances use the original baseline-aligned pixel canvas. Empty/undefined
measurements use None, not a perfect score. Reference outlines are not observers.
"""
from dataclasses import dataclass
import math
import warnings

import gudhi
import numpy as np
from scipy import ndimage as ndi
from skimage.metrics import structural_similarity
from skimage.morphology import skeletonize

with warnings.catch_warnings():
    warnings.simplefilter("ignore", UserWarning)  # phasepack's optional FFT accelerator
    from phasepack import phasecong


def stats(values, weights=None):
    values = np.asarray(values, dtype=float)
    if values.ndim != 1:
        raise ValueError("Statistics require a one-dimensional value sequence")
    if weights is not None:
        weights = np.asarray(weights, dtype=float)
        if weights.shape != values.shape or np.any(weights < 0) or not np.all(np.isfinite(weights)):
            raise ValueError("Invalid frequency weights")
    valid = np.isfinite(values)
    values = values[valid]
    if not values.size:
        return {"count": 0, "mean": None, "p5": None, "p95": None, "min": None, "max": None, "std": None, "abs_p95": None}
    if weights is None:
        mean, p95 = np.mean(values), np.percentile(values, 95)
        p5, std = np.percentile(values, 5), np.std(values)
        abs_p95 = np.percentile(np.abs(values), 95)
    else:
        weights = weights[valid]
        present = weights > 0
        values, weights = values[present], weights[present]
        if not values.size:
            return {"count": 0, "mean": None, "p5": None, "p95": None, "min": None, "max": None, "std": None, "abs_p95": None}
        order = np.argsort(values)
        mean = np.average(values, weights=weights)
        p95 = values[order][np.searchsorted(np.cumsum(weights[order]), .95*weights.sum())]
        p5 = values[order][np.searchsorted(np.cumsum(weights[order]), .05*weights.sum())]
        std = math.sqrt(float(np.average((values-mean)**2, weights=weights)))
        abs_order = np.argsort(np.abs(values))
        abs_p95 = np.abs(values)[abs_order][np.searchsorted(np.cumsum(weights[abs_order]), .95*weights.sum())]
    return {"count": len(values), "mean": float(mean), "p5": float(p5), "p95": float(p95), "min": float(values.min()), "max": float(values.max()), "std": float(std), "abs_p95": float(abs_p95)}


def skeleton_length(skeleton):
    """Euclidean graph length, counting each edge once, without corner shortcuts."""
    result = 0.
    for dy, dx in ((0, 1), (1, 0), (1, 1), (1, -1)):
        padded = np.pad(skeleton, 1)
        neighbor = padded[1+dy:1+dy+skeleton.shape[0], 1+dx:1+dx+skeleton.shape[1]]
        edges = skeleton & neighbor
        if dx and dy:
            cardinal_x = padded[1:1+skeleton.shape[0], 1+dx:1+dx+skeleton.shape[1]]
            cardinal_y = padded[1+dy:1+dy+skeleton.shape[0], 1:1+skeleton.shape[1]]
            edges &= ~cardinal_x & ~cardinal_y
        result += int(edges.sum()) * math.hypot(dx, dy)
    return result


def directional_widths(mask, skeleton):
    """Local PCA tangent + normal cell-boundary ray widths in native pixels.

Exclude branch/end points and their one-pixel neighborhood. Ray step 1/8px;
axis-aligned constant-width bars recover their pixel-cell widths (unlike 2 EDT).
This remains a raster proxy; diagonal rasterization has quantization error.
"""
    buckets = {key: [] for key in ("horizontal", "diagonal_down", "vertical", "diagonal_up")}
    degrees = ndi.convolve(skeleton.astype(int), np.ones((3, 3), int), mode="constant") - skeleton
    eligible = skeleton & ~ndi.binary_dilation(skeleton & (degrees != 2))
    coords = np.argwhere(skeleton)
    padded = np.pad(mask, 1)
    ray = np.arange(.0625, math.hypot(*mask.shape)+1, .125)
    for y, x in np.argwhere(eligible):
        local = coords[(np.abs(coords[:, 0]-y) <= 2) & (np.abs(coords[:, 1]-x) <= 2)]
        if len(local) < 3:
            continue
        eigenvalues, eigenvectors = np.linalg.eigh(np.cov(local.T))
        if eigenvalues[-1] <= 0 or eigenvalues[0]/eigenvalues[-1] > .3:
            continue  # corner, not a locally straight stroke
        tangent = eigenvectors[:, -1]
        normal = np.array([-tangent[1], tangent[0]])
        angle = math.degrees(math.atan2(tangent[0], tangent[1])) % 180
        key = list(buckets)[int((angle+22.5)//45) % 4]
        distances = []
        for sign in (-1, 1):
            positions = np.array([[y+1], [x+1]]) + sign*normal[:, None]*ray
            inside = ndi.map_coordinates(padded.astype(float), positions, order=0, mode="constant") > .5
            first = np.flatnonzero(~inside)
            # First outside sample brackets the cell boundary; use its midpoint.
            distances.append(float(ray[first[0]]-.0625) if first.size else float(ray[-1]))
        buckets[key].append(sum(distances))
    return {key: stats(values) for key, values in buckets.items()}


def balance(image):
    area = float(image.sum())
    if area == 0:
        return {"centroid_x": None, "centroid_y": None, "moment_xx": None,
                "moment_yy": None, "moment_xy": None, "principal_angle_deg": None,
                "anisotropy": None, "quadrant_density": [0.]*4}
    y, x = np.indices(image.shape, dtype=float)
    cx, cy = float((image*x).sum()/area), float((image*y).sum()/area)
    xx, yy, xy = (float((image*v).sum()/area) for v in ((x-cx)**2, (y-cy)**2, (x-cx)*(y-cy)))
    eigenvalues, eigenvectors = np.linalg.eigh([[xx, xy], [xy, yy]])
    axis = eigenvectors[:, -1]
    mid_y, mid_x = np.array(image.shape)//2
    quadrants = [image[:mid_y, :mid_x], image[:mid_y, mid_x:], image[mid_y:, :mid_x], image[mid_y:, mid_x:]]
    return {"centroid_x": cx, "centroid_y": cy, "moment_xx": xx, "moment_yy": yy, "moment_xy": xy,
            "principal_angle_deg": math.degrees(math.atan2(axis[1], axis[0])) % 180,
            "anisotropy": float((eigenvalues[-1]-eigenvalues[0])/max(1e-12, eigenvalues.sum())),
            "quadrant_density": [float(q.mean()) if q.size else 0. for q in quadrants]}


def signed_distance(mask):
    padded = np.pad(mask, 1)
    return (ndi.distance_transform_edt(padded)-ndi.distance_transform_edt(~padded))[1:-1, 1:-1]


def persistence(image):
    """Coverage superlevel cubical filtration, dimensions 0 and 1.

Compare finite intervals; essential counts reported separately. Cubical
connectivity differs from the generator's 4-foreground/8-background counts.
"""
    complex_ = gudhi.CubicalComplex(top_dimensional_cells=1-np.pad(image, 1))
    complex_.persistence()
    result = {}
    for dim in (0, 1):
        diagram = complex_.persistence_intervals_in_dimension(dim)
        finite = diagram[np.isfinite(diagram).all(axis=1)]
        result[str(dim)] = {"diagram": finite, "essential": int((~np.isfinite(diagram).all(axis=1)).sum())}
    return result


@dataclass
class Features:
    coverage: np.ndarray

    def __post_init__(self):
        self.coverage = np.asarray(self.coverage, dtype=float)
        if self.coverage.ndim != 2 or not self.coverage.size or not np.all(np.isfinite(self.coverage)):
            raise ValueError("Expected a finite nonempty 2-D glyph canvas")
        if self.coverage.min() < 0 or self.coverage.max() > 1:
            raise ValueError("Coverage outside [0,1]")
        self.mask = self.coverage >= .5
        self.skeleton = skeletonize(self.mask)
        self.distance = signed_distance(self.mask)
        self.edge = self.mask & ~ndi.binary_erosion(self.mask)
        self.balance = balance(self.coverage)
        self.length = skeleton_length(self.skeleton)
        self.widths = directional_widths(self.mask, self.skeleton)
        self.persistence = persistence(self.coverage)
        # White surround prevents tiny glyphs wrapping around in the FFT.
        padded = np.pad(self.coverage*255, 8)
        if np.any(padded):
            with np.errstate(divide="ignore", invalid="ignore"):
                self.phase = np.nan_to_num(np.sum(phasecong(padded, nscale=4, norient=4,
                    minWaveLength=6, mult=2, sigmaOnf=.55)[4], axis=0))
        else:
            self.phase = np.zeros_like(padded)
        gx = ndi.convolve(padded, np.array([[3, 0, -3], [10, 0, -10], [3, 0, -3]])/16)
        gy = ndi.convolve(padded, np.array([[3, 10, 3], [0, 0, 0], [-3, -10, -3]])/16)
        self.gradient = np.hypot(gx, gy)

    def descriptors(self):
        coords = np.argwhere(self.mask)
        height = int(np.ptp(coords[:, 0])+1) if coords.size else 0
        width = int(np.ptp(coords[:, 1])+1) if coords.size else 0
        effective = float(self.mask.sum())/self.length if self.length else None
        perimeter = float(self.edge.sum())  # pixel boundary count, not continuous perimeter
        local = ndi.uniform_filter(self.coverage, 3, mode="constant")
        return {"ink_density": float(self.coverage.mean()), "ink_area": float(self.coverage.sum()),
                "ink_height_px": height, "ink_width_px": width,
                "ink_bottom_px": int(coords[:, 0].max()+1) if coords.size else None,
                "left_bearing_px": int(coords[:, 1].min()) if coords.size else None,
                "right_bearing_px": int(self.mask.shape[1]-1-coords[:, 1].max()) if coords.size else None,
                "advance_px": self.mask.shape[1], "line_height_px": self.mask.shape[0],
                "skeleton_length_px": self.length, "effective_width_px": effective,
                "normalized_effective_width": effective/height if effective is not None and height else None,
                "bbox_density": float(self.mask.sum())/(height*width) if height*width else None,
                "perimetric_complexity_proxy": perimeter**2/max(1, int(self.mask.sum())),
                "local_black_concentration_p95": float(np.percentile(local[self.mask], 95)) if self.mask.any() else None,
                "directional_widths": self.widths,
                "directional_width_cv": {k: v["std"]/v["mean"] if v["mean"] else None for k, v in self.widths.items()},
                "directional_thickening_ratio": {k: v["p95"]/v["mean"] if v["mean"] else None for k, v in self.widths.items()},
                "balance": self.balance,
                "persistence": {d: {"finite_intervals": len(v["diagram"]), "essential": v["essential"],
                    "lifetimes": (v["diagram"][:, 1]-v["diagram"][:, 0]).tolist()} for d, v in self.persistence.items()}}


def image_metrics(a, b):
    if a.coverage.shape != b.coverage.shape:
        raise ValueError("Metrics require a common baseline-aligned canvas")
    mse = float(np.mean((a.coverage-b.coverage)**2))
    if a.edge.any() and b.edge.any():
        distances = np.concatenate([ndi.distance_transform_edt(~a.edge)[b.edge], ndi.distance_transform_edt(~b.edge)[a.edge]])
        boundary = {"p95": float(np.percentile(distances, 95)), "max": float(distances.max())}
    elif a.edge.any() or b.edge.any():
        boundary = {"p95": None, "max": None}  # missing entire glyph, not an arbitrary huge sentinel
    else:
        boundary = {"p95": 0., "max": 0.}
    similarities, contrasts = [], []
    x, y = a.coverage.copy(), b.coverage.copy()
    while min(x.shape) >= 3 and len(similarities) < 5:
        extent = min(x.shape)
        win = min(7, extent if extent % 2 else extent-1)
        score = structural_similarity(x, y, data_range=1, win_size=win)
        similarities.append(float(score))
        ux, uy = ndi.uniform_filter(x, win), ndi.uniform_filter(y, win)
        vx, vy = ndi.uniform_filter(x*x, win)-ux*ux, ndi.uniform_filter(y*y, win)-uy*uy
        cov = ndi.uniform_filter(x*y, win)-ux*uy
        cs = (2*cov+.03**2)/(np.maximum(vx+vy, 0)+.03**2)
        crop = win//2
        contrasts.append(float(cs[crop:-crop, crop:-crop].mean()))
        x, y = ndi.uniform_filter(x, 2)[::2, ::2], ndi.uniform_filter(y, 2)[::2, ::2]
    weights = np.array([.0448, .2856, .3001, .2363, .1333][:len(similarities)])
    weights /= weights.sum() if weights.size else 1
    terms = contrasts[:-1] + similarities[-1:]
    ms = float(np.prod(np.maximum(terms, 0)**weights)) if terms else None
    pc = np.maximum(a.phase, b.phase)
    sim_pc = (2*a.phase*b.phase+.85)/(a.phase*a.phase+b.phase*b.phase+.85)
    sim_g = (2*a.gradient*b.gradient+160)/(a.gradient*a.gradient+b.gradient*b.gradient+160)
    fsim = float((sim_pc*sim_g*pc).sum()/pc.sum()) if pc.sum() else (1. if mse == 0 else None)
    return {"mse": mse, "psnr_db": -10*math.log10(mse) if mse else None, "psnr_exact": mse == 0,
            "boundary_distance_px": boundary, "signed_distance_mae_px": float(np.abs(a.distance-b.distance).mean()),
            "ssim": similarities[0] if similarities else None, "adaptive_ms_ssim": ms,
            "ms_ssim_scales": len(similarities), "fsim_phasepack": fsim}


def compare(a, b):
    result = {"image": image_metrics(a, b), "directional_width_error_px": {}, "directional_ratio_error": {}}
    for key in a.widths:
        x, y = a.widths[key]["mean"], b.widths[key]["mean"]
        result["directional_width_error_px"][key] = x-y if x is not None and y is not None else None
    for key in ("horizontal", "diagonal_down", "diagonal_up"):
        x, y, xv, yv = a.widths[key]["mean"], b.widths[key]["mean"], a.widths["vertical"]["mean"], b.widths["vertical"]["mean"]
        result["directional_ratio_error"][key+"_over_vertical"] = x/xv-y/yv if all(v is not None and v > 0 for v in (x, y, xv, yv)) else None
    cx, cy, rx, ry = (d[k] for d, k in ((a.balance, "centroid_x"), (a.balance, "centroid_y"), (b.balance, "centroid_x"), (b.balance, "centroid_y")))
    result["centroid_drift_px"] = math.hypot(cx-rx, cy-ry) if all(v is not None for v in (cx, cy, rx, ry)) else None
    result["moment_error_px2"] = {k: a.balance[k]-b.balance[k] if a.balance[k] is not None and b.balance[k] is not None else None for k in ("moment_xx", "moment_yy", "moment_xy")}
    result["quadrant_density_error"] = (np.array(a.balance["quadrant_density"])-b.balance["quadrant_density"]).tolist()
    aa, ba = a.balance["principal_angle_deg"], b.balance["principal_angle_deg"]
    result["principal_angle_drift_deg"] = abs((aa-ba+90) % 180-90) if aa is not None and ba is not None and min(a.balance["anisotropy"], b.balance["anisotropy"]) > .1 else None
    da, db = a.descriptors(), b.descriptors()
    result["weight_error"] = {k: da[k]-db[k] if da[k] is not None and db[k] is not None else None
                             for k in ("ink_density", "ink_area", "effective_width_px", "normalized_effective_width", "local_black_concentration_p95")}
    result["persistence_bottleneck"] = {d: float(gudhi.bottleneck_distance(a.persistence[d]["diagram"], b.persistence[d]["diagram"], e=0)) for d in ("0", "1")}
    # Count overlap correspondences: a reference component splitting, or target merging.
    la, na = ndi.label(a.mask)
    lb, nb = ndi.label(b.mask)
    result["component_splits"] = sum(len(set(la[lb == i])-{0}) > 1 for i in range(1, nb+1))
    result["component_merges"] = sum(len(set(lb[la == i])-{0}) > 1 for i in range(1, na+1))
    missing = b.skeleton & ~a.mask
    lm, nm = ndi.label(missing, np.ones((3, 3)))
    result["missing_stroke_patches"] = nm
    result["largest_missing_patch_px"] = int(np.bincount(lm.ravel())[1:].max()) if nm else 0
    return result


def padded_vectors(images, sigma=.65):
    height = max(a.shape[0] for a in images.values())
    width = max(a.shape[1] for a in images.values())
    # Top/left alignment preserves the common baseline and side bearing.
    return np.array([ndi.gaussian_filter(np.pad(a.astype(float), ((0, height-a.shape[0]), (0, width-a.shape[1]))), sigma).ravel() for a in images.values()])


def confusion(masks, references, neighbors=5):
    """All-glyph nearest neighbors and outline-referenced recognition margin.

Squared blurred-pixel distance is a model, not a human confusion probability.
No per-glyph centering or scaling which could conceal layout errors.
"""
    chars = list(masks)
    vectors = padded_vectors(masks)
    refs = padded_vectors({c: references[c] for c in chars})
    def distances(x, y):
        return np.maximum(0, (x*x).sum(axis=1)[:, None]+(y*y).sum(axis=1)[None, :]-2*x@y.T)/x.shape[1]
    mutual = distances(vectors, vectors)
    to_ref = distances(vectors, refs)
    reference_distances = distances(refs, refs)
    result = {}
    for i, char in enumerate(chars):
        mutual[i, i] = np.inf
        order = np.argsort(mutual[i])[:min(neighbors, len(chars)-1)]
        others = to_ref[i].copy()
        own = float(others[i])
        others[i] = np.inf
        rival = int(np.argmin(others))
        distinguishing = np.abs(refs[i]-refs[rival]) > .05
        distinguishing_error = float(np.mean(np.abs(vectors[i]-refs[i])[distinguishing])) if distinguishing.any() else None
        result[char] = {"nearest": [{"char": chars[j], "distance": float(mutual[i, j]),
            "reference_distance": float(reference_distances[i, j]),
            "collapsed": bool(np.array_equal(vectors[i], vectors[j]))} for j in order],
            "reference_margin": float(others[rival]-own), "reference_rival": chars[rival],
            "distinctive_region_error": distinguishing_error}
    return result


def robustness(mask, reference):
    """Declared sensitivity scenarios, not calibrated EPD physics."""
    reference = np.asarray(reference, float)
    result = []
    cases = [("blur", s, ndi.gaussian_filter(mask.astype(float), s)) for s in (.0, .4, .8, 1.2)]
    cases += [("contrast", c, .5+c*(mask.astype(float)-.5)) for c in (1., .75, .5)]
    cases += [("phase_x", p, ndi.shift(mask.astype(float), (0, p), order=1, mode="constant", prefilter=False)) for p in (-.5, .25, .5)]
    cases += [("phase_y", p, ndi.shift(mask.astype(float), (p, 0), order=1, mode="constant", prefilter=False)) for p in (-.5, .25, .5)]
    # Synthetic one-pixel previous-image residue; no claim this models panel ghosting.
    cases += [("residue", r, (1-r)*mask+r*ndi.shift(mask.astype(float), (0, 1), order=0, mode="constant")) for r in (.05, .15)]
    for name, parameter, image in cases:
        result.append({"scenario": name, "parameter": parameter, "coverage_mse": float(np.mean((image-reference)**2)),
                       "contrast_span": float(image.max()-image.min()),
                       "missing_centerline_fraction": float((skeletonize(reference >= .5) & ~(image >= .5)).sum())/max(1, int(skeletonize(reference >= .5).sum()))})
    return result
