"""Continuous CJK ink/core/Fourier diagnostics, in native pixel coordinates.

The Gaussian central-ink window is a reproducible geometric proxy, not a
semantic identification of a character's 中宫. Direction and salience models
are uncalibrated. Power spectra cannot determine translational displacement.
"""
import numpy as np
from scipy import ndimage as ndi
from scipy.stats import entropy
from cjk_font_optimizer import Reference, centroid, cell_distances


def moments(a):
    c = centroid(a)
    if c is None:
        return None
    y, x = np.indices(a.shape)
    xx, yy, xy = ((a*v).sum()/a.sum() for v in ((x-c[0])**2, (y-c[1])**2, (x-c[0])*(y-c[1])))
    trace = xx+yy
    q = np.array([xx-yy, 2*xy])/max(trace, 1e-12)
    return c, q, float(trace), float(np.linalg.norm(q)), float(np.arctan2(2*xy, xx-yy)/2)


def orientation_labels(r):
    gx = ndi.gaussian_filter(r, .6, order=(0, 1), mode="constant")
    gy = ndi.gaussian_filter(r, .6, order=(1, 0), mode="constant")
    xx, yy, xy = [ndi.gaussian_filter(v, .7, mode="constant") for v in (gx*gx, gy*gy, gx*gy)]
    coherence = np.hypot(xx-yy, 2*xy)/np.maximum(xx+yy, 1e-12)
    axis = np.mod(.5*np.arctan2(2*xy, xx-yy)+np.pi/2, np.pi)
    labels = np.mod(np.floor((axis+np.pi/8)/(np.pi/4)).astype(int), 4)
    labels[(coherence < .25) | (ndi.gaussian_filter(r, .7, mode="constant") < .03)] = 4
    return labels


def fourier(mask, reference):
    n = 64
    if max(mask.shape) > n:
        raise ValueError("64-point Fourier grid is only defined for the native UI sizes")
    fm, fr = [np.fft.fft2(a/a.sum(), s=(n, n)) for a in (mask, reference)]
    fy, fx = np.meshgrid(np.fft.fftfreq(n), np.fft.fftfreq(n), indexing="ij")
    radius = np.hypot(fx, fy)
    cross = fm*fr.conj()
    shift = -np.angle([cross[0, 1], cross[1, 0]])/(2*np.pi/n)
    power, expected = np.abs(fm)**2, np.abs(fr)**2
    low = (radius > 0) & (radius <= .1)
    directions = np.mod(np.floor((np.mod(np.arctan2(fy, fx), np.pi)+np.pi/8)/(np.pi/4)).astype(int), 4)
    vectors = [np.array([(p*radius**2)[directions == i].sum() for i in range(4)]) for p in (power, expected)]
    p, q = [v/v.sum() for v in vectors]
    result = {"phase_dx": float(shift[0]), "phase_dy": float(shift[1]), "phase_drift": float(np.linalg.norm(shift)),
        "low_phase_rms_deg": float(np.sqrt(np.average(np.angle(cross[low])**2, weights=expected[low]))*180/np.pi),
        "orientation_js_bits": float((entropy(p, (p+q)/2, base=2)+entropy(q, (p+q)/2, base=2))/2),
        "high_energy_change_pp": float(100*(power[radius > .25].sum()/power[radius > 0].sum()
                                             -expected[radius > .25].sum()/expected[radius > 0].sum()))}
    for name, band in [("low", low), ("mid", (radius > .1) & (radius <= .25)), ("high", radius > .25)]:
        result[name+"_complex_rms"] = float(np.sqrt(np.mean(np.abs(fm[band]-fr[band])**2)))
    return result


def measure(mask, reference, outline=None):
    model = Reference(reference, outline)
    mask = np.asarray(mask, bool)
    if mask.shape != model.coverage.shape or not model.mass or not mask.any():
        raise ValueError("Balance requires nonempty reference and mask on the same native canvas")
    r, m = model.coverage, mask.astype(float)
    result = model.candidate("measured", mask).metrics.copy()
    if outline is not None:
        stroke_distance = cell_distances(model.stroke_points, np.argwhere(mask))
        result["outline_stroke_distance_p95"] = float(np.percentile(stroke_distance, 95)) if len(stroke_distance) else None
        result["outline_stroke_distance_max"] = float(stroke_distance.max()) if len(stroke_distance) else None
        result["outline_missing_stroke_fraction"] = float(np.mean(stroke_distance >= .5)) if len(stroke_distance) else None
    dc = centroid(m)-model.center
    plus, minus = np.maximum(m-r, 0), np.maximum(r-m, 0)
    y, x = np.indices(m.shape)
    positions = np.stack([x-model.center[0], y-model.center[1]], axis=-1)
    added = (plus[..., None]*positions).sum(axis=(0, 1))/m.sum()
    lost = -(minus[..., None]*positions).sum(axis=(0, 1))/m.sum()
    np.testing.assert_allclose(added+lost, dc, atol=1e-12)
    core, rc = moments(m*model.window), moments(r*model.window)
    valid_tilt = min(core[3], rc[3]) >= .1
    tilt = (core[4]-rc[4]+np.pi/2) % np.pi-np.pi/2
    result.update({"ink_dx": float(dc[0]), "ink_dy": float(dc[1]),
        "core_dx": float(core[0][0]-rc[0][0]), "core_dy": float(core[0][1]-rc[0][1]),
        "core_quadrupole_error": float(np.linalg.norm(core[1]-rc[1])),
        "core_tilt_abs_deg": float(abs(tilt)*180/np.pi) if valid_tilt else None,
        "core_spread_change_pct": float(100*(core[2]/rc[2]-1)) if rc[2] else None,
        "coverage_mass_change_pct": float(100*(m.sum()/model.mass-1)),
        "added_coverage_pct": float(100*plus.sum()/model.mass),
        "missing_coverage_pct": float(100*minus.sum()/model.mass),
        "added_dx": float(added[0]), "added_dy": float(added[1]), "added_pull": float(np.linalg.norm(added)),
        "missing_dx": float(lost[0]), "missing_dy": float(lost[1]), "missing_pull": float(np.linalg.norm(lost)),
        "added_missing_dot": float(added@lost)})
    extra = mask & (r < .5)
    labels, n = ndi.label(extra, np.ones((3, 3)))
    sizes = np.bincount(labels.ravel())[1:] if n else []
    result.update({"extra_threshold_pixels": int(extra.sum()), "extra_cluster_count": int(n),
                   "extra_largest_cluster": int(max(sizes)) if len(sizes) else 0})
    for cutoff in (.05, .1, .2):
        result[f"strong_extra_{cutoff}_pixels"] = int(np.sum(mask & (r <= cutoff)))
    coords = np.argwhere(r > 0)
    top, left = coords.min(axis=0)-.5
    bottom, right = coords.max(axis=0)+.5
    cx, cy = (left+right)/2, (top+bottom)/2
    for fraction in (1/3, .5, 2/3):
        rx, ry = (right-left)*fraction/2, (bottom-top)*fraction/2
        ox = np.maximum(0, np.minimum(np.arange(m.shape[1])+.5, cx+rx)-np.maximum(np.arange(m.shape[1])-.5, cx-rx))
        oy = np.maximum(0, np.minimum(np.arange(m.shape[0])+.5, cy+ry)-np.maximum(np.arange(m.shape[0])-.5, cy-ry))
        roi = oy[:, None]*ox[None, :]
        for kind, a, b in [("ink", m, r), ("white", 1-m, 1-r)]:
            ca, cb = centroid(a*roi), centroid(b*roi)
            result[f"core{fraction}_{kind}_drift"] = float(np.linalg.norm(ca-cb)) if ca is not None and cb is not None else None
        result[f"core{fraction}_density_change_pp"] = float(100*((m-r)*roi).sum()/roi.sum())
    labels = orientation_labels(r)
    extras = []
    for i, name in enumerate(("horizontal", "diagonal_down", "vertical", "diagonal_up", "uncertain")):
        region = labels == i
        denom = r[region].sum()
        excess = float(100*plus[region].sum()/denom) if denom > .25 else None
        result[f"direction_{name}_extra_pct"] = excess
        result[f"direction_{name}_missing_pct"] = float(100*minus[region].sum()/denom) if denom > .25 else None
        if i < 4 and excess is not None:
            extras.append(excess)
    result["direction_extra_range_pp"] = max(extras)-min(extras) if len(extras) > 1 else None
    result.update(fourier(m, r))
    return result
