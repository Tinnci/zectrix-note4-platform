"""Offline, outline-constrained CJK candidate selection; no runtime processing.

Coverage is continuous, including strokes below the binary threshold. An accepted
candidate cannot worsen the previous glyph's coverage, mass, centroid, central
ink centroid, local-darkness centroid or strong out-of-outline pixel count.
These are engineering proxies, not a calibrated readability score.
"""
from dataclasses import dataclass
import numpy as np
from scipy import ndimage as ndi
from scipy.optimize import linear_sum_assignment
from scipy.spatial import cKDTree

THRESHOLD = 128 / 255


def topology(mask):
    """Four-connected ink / eight-connected white, matching the existing guard."""
    return (int(ndi.label(mask)[1]),
            int(ndi.label(~np.pad(mask, 1), np.ones((3, 3)))[1] - 1))


def centroid(ink):
    mass = float(ink.sum())
    if mass <= 0:
        return None
    y, x = np.indices(ink.shape)
    return np.array([(ink*x).sum(), (ink*y).sum()]) / mass


def darkness(ink):
    # Padding avoids a clipped kernel moving the centroid near a canvas edge.
    return ndi.gaussian_filter(np.pad(ink.astype(float), 4), .5, mode="constant")**2


@dataclass
class Candidate:
    name: str
    mask: np.ndarray
    metrics: dict
    topology_error: int


def cell_distances(points, cells):
    """Exact distance from subpixel points to the union of native pixel squares."""
    if not len(points):
        return np.empty(0)
    if not len(cells):
        return np.full(len(points), np.inf)
    # Query a few centers, then use the square's circumscribed radius to prove
    # no unqueried square can be closer. Expand only unresolved points. This is
    # exact, not a fixed-k approximation, and avoids a points × all-cells tensor.
    tree = cKDTree(cells)
    active = np.arange(len(points))
    result = np.full(len(points), np.inf)
    k = min(8, len(cells))
    while len(active):
        distances, indices = tree.query(points[active], k=k)
        distances, indices = distances.reshape(len(active), k), indices.reshape(len(active), k)
        delta = np.maximum(np.abs(points[active, None, :]-cells[indices])-.5, 0)
        nearest = np.sqrt(np.min(np.sum(delta*delta, axis=-1), axis=1))
        result[active] = nearest
        if k == len(cells):
            break
        active = active[distances[:, -1] < nearest+np.sqrt(.5)]
        k = min(k*2, len(cells))
    return result


def cell_distance(points, cells):
    distances = cell_distances(points, cells)
    return float(distances.mean()) if len(distances) else None


def hole_matches(edges):
    """Maximum one-to-one overlap, so two reference holes cannot share one hole."""
    if not edges.size:
        return 0
    rows, columns = linear_sum_assignment(-edges.astype(int))
    return int(edges[rows, columns].sum())


class Reference:
    def __init__(self, coverage, outline=None):
        self.coverage = np.asarray(coverage, float)
        if (self.coverage.ndim != 2 or not self.coverage.size or
                not np.isfinite(self.coverage).all() or
                self.coverage.min() < 0 or self.coverage.max() > 1):
            raise ValueError("Expected finite nonempty 2-D coverage in [0, 1]")
        self.mass = float(self.coverage.sum())
        self.center = centroid(self.coverage)
        self.topology = topology(self.coverage >= THRESHOLD)
        self.blur = ndi.gaussian_filter(np.pad(self.coverage, 4), .5, mode="constant")
        self.dark_center = centroid(darkness(self.coverage))
        coords = np.argwhere(self.coverage > 0)
        self.window = np.ones_like(self.coverage)
        if len(coords):
            top, left = coords.min(axis=0)
            bottom, right = coords.max(axis=0)
            x, y = np.meshgrid(np.arange(self.coverage.shape[1]), np.arange(self.coverage.shape[0]))
            sx, sy = (right-left+1)/4, (bottom-top+1)/4
            self.window = np.exp(-.5*((x-(left+right)/2)/sx)**2
                                 -.5*((y-(top+bottom)/2)/sy)**2)
        self.core_center = centroid(self.coverage*self.window)
        self.outline = None
        self.tolerances = {}
        if outline is not None:
            from skimage.morphology import skeletonize
            outline = np.asarray(outline, float)
            if outline.ndim != 2:
                raise ValueError("Outline must be a 2-D supersampled canvas")
            scale = outline.shape[0]//self.coverage.shape[0]
            if (scale < 1 or
                    outline.shape != tuple(v*scale for v in self.coverage.shape) or
                    not np.isfinite(outline).all() or outline.min() < 0 or outline.max() > 1):
                raise ValueError("Outline must be finite supersampled coverage on the same canvas")
            ink = outline >= THRESHOLD
            self.outline = outline
            # A half supersample pixel is the declared reference-position
            # precision. It is not a measured panel/readability tolerance.
            self.tolerances["outline_white_distance"] = .5/scale
            self.stroke_points = (np.argwhere(skeletonize(ink))+.5)/scale-.5
            # Only white medial axes with radius >= half a native pixel are
            # resolvable one-pixel channels. Finer gaps remain a diagnostic,
            # not a demand the 1bpp grid cannot represent.
            white = ~ink
            radius = ndi.distance_transform_edt(white)/scale
            white_skeleton = skeletonize(white) & (radius >= .5)
            support = np.argwhere(ink)
            if len(support):
                top, left = support.min(axis=0)
                bottom, right = support.max(axis=0)
                white_skeleton[:top+1] = False
                white_skeleton[bottom:] = False
                white_skeleton[:, :left+1] = False
                white_skeleton[:, right:] = False
            self.white_points = (np.argwhere(white_skeleton)+.5)/scale-.5
            labels, count = ndi.label(~np.pad(ink, 1), np.ones((3, 3)))
            outside = labels[0, 0]
            self.holes = []
            self.resolved_holes = []
            for i in range(1, count+1):
                if i == outside:
                    continue
                coords = np.argwhere(labels[1:-1, 1:-1] == i)
                cells = np.unique(coords//scale, axis=0)
                self.holes.append(cells)
                self.resolved_holes.append(len(coords) >= scale*scale)

    def candidate(self, name, mask):
        mask = np.asarray(mask, bool)
        if mask.shape != self.coverage.shape:
            raise ValueError("Candidate and reference must share the unchanged glyph canvas")
        if not self.mass:
            # Spaces remain blank; adding ink is never an optimization.
            error = float(mask.sum())
            metrics = dict.fromkeys(("ink_drift", "core_drift", "darkness_drift", "blur_error", "mass_error"), error)
        elif not mask.any():
            metrics = dict.fromkeys(("ink_drift", "core_drift", "darkness_drift", "blur_error", "mass_error"), float("inf"))
        else:
            blur = ndi.gaussian_filter(np.pad(mask.astype(float), 4), .5, mode="constant")
            metrics = {
                "ink_drift": float(np.linalg.norm(centroid(mask)-self.center)),
                "core_drift": float(np.linalg.norm(centroid(mask*self.window)-self.core_center)),
                "darkness_drift": float(np.linalg.norm(centroid(darkness(mask))-self.dark_center)),
                "blur_error": float(np.mean((blur-self.blur)**2)),
                "mass_error": abs(float(mask.sum())-self.mass)/self.mass,
            }
        metrics["strong_extra"] = int(np.sum(mask & (self.coverage <= .1)))
        if self.outline is not None:
            metrics["outline_stroke_distance"] = cell_distance(self.stroke_points, np.argwhere(mask))
            white_distance = cell_distances(self.white_points, np.argwhere(~mask))
            metrics["outline_white_distance"] = float(white_distance.mean()) if len(white_distance) else None
            # A medial axis covered by half a native pixel has no nearby white
            # pixel square to represent the originally >=1px white channel.
            metrics["outline_white_closed_fraction"] = float(np.mean(white_distance >= .5)) if len(white_distance) else None
            labels, count = ndi.label(~np.pad(mask, 1), np.ones((3, 3)))
            outside = labels[0, 0]
            labels = labels[1:-1, 1:-1]
            target_holes = [i for i in range(1, count+1) if i != outside]
            edges = np.array([[np.any(labels[c[:, 0], c[:, 1]] == i) for i in target_holes]
                              for c in self.holes], bool).reshape(len(self.holes), len(target_holes))
            resolved = np.asarray(self.resolved_holes, bool)
            metrics["outline_lost_holes"] = int(resolved.sum())-hole_matches(edges[resolved])
            metrics["outline_extra_holes"] = len(target_holes)-hole_matches(edges)
        error = sum(abs(a-b) for a, b in zip(topology(mask), self.topology))
        return Candidate(name, mask, metrics, error)


def select(reference, previous, native, candidates):
    """Return a constrained, finite-precision improvement over previous rendering.

    Keep the previous native component/hole-count protection. Among admissible
    non-regressing candidates, first eliminate strong extras, then minimize the
    worst of the three centroid distances (all in native pixels). Coverage error
    resolves ties. No weighted cross-unit 'readability score' is introduced.
    The unchanged previous mask is always available, so impossible tradeoffs do
    not silently change a glyph. True outline stroke distance, white-channel
    closure and hole correspondence cannot worsen. Mean white displacement has
    only the declared half-reference-sample tolerance. These avoid
    measuring stroke loss only against a thresholded native reference that has
    already erased the same thin stroke. Equal candidates retain the previous.
    """
    old = reference.candidate("previous", previous)
    native_error = reference.candidate("native-gray", native).topology_error
    if old.topology_error > native_error:
        raise ValueError("Previous glyph violates the existing native topology protection")
    eligible = [old]
    seen = {previous.tobytes()}
    for name, mask in candidates:
        mask = np.asarray(mask, bool)
        if mask.shape != reference.coverage.shape:
            raise ValueError("Candidate changes the glyph canvas")
        key = mask.tobytes()
        if key in seen:
            continue
        seen.add(key)
        # Reject inexpensive mass/position regressions before Gaussian kernels.
        mass = float(mask.sum())
        if not mass or not reference.mass:
            continue
        if abs(mass-reference.mass)/reference.mass > old.metrics["mass_error"]+1e-12:
            continue
        if np.sum(mask & (reference.coverage <= .1)) > old.metrics["strong_extra"]:
            continue
        if np.linalg.norm(centroid(mask)-reference.center) > old.metrics["ink_drift"]+1e-12:
            continue
        if np.linalg.norm(centroid(mask*reference.window)-reference.core_center) > old.metrics["core_drift"]+1e-12:
            continue
        if sum(abs(a-b) for a, b in zip(topology(mask), reference.topology)) > native_error:
            continue
        item = reference.candidate(name, mask)
        if all((value is None if old.metrics[key] is None else
                value is not None and value <= old.metrics[key] + reference.tolerances.get(key, 0) + 1e-12)
               for key, value in item.metrics.items()):
            eligible.append(item)
    def order(item):
        m = item.metrics
        return (m["strong_extra"], max(m["ink_drift"], m["core_drift"], m["darkness_drift"]),
                m["blur_error"], m["mass_error"], m["ink_drift"], m["core_drift"], m["darkness_drift"])
    chosen = min(eligible, key=order)
    return chosen, old, len(eligible)
