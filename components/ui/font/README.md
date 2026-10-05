# System fonts

## Editorial interface

Navigation and page titles use a bounded [Noto Sans CJK SC 2.004](https://github.com/notofonts/noto-cjk/releases/tag/Sans2.004)
subset under [SIL OFL 1.1](OFL-NotoSansCJK.txt). Native raster sizes are 14px
regular captions, 12px ASCII radio labels, 16px regular labels (page counters, reading prompt), 18px regular/bold navigation, 18px bold page titles, and 22px bold book names.
Chinese bold uses actual bold outlines, not Unifont's underline emphasis.
Reading pages retain a native 16px bold compact header to preserve pagination;
the home reading prompt uses regular 16px to emphasize the bold book name.
The six faces contain 575 characters (ASCII, UI catalog and preview names),
plus 95 ASCII characters in the 12px face, packed at 1bpp without row padding;
Blank glyph rows are omitted losslessly, retaining line height, baseline and
advance. CJK glyphs are optimized offline against continuous 16x unhinted
outline coverage, including strokes below the binary threshold. Candidates are
native grayscale, Pillow 4x, unhinted 2x, and unhinted 8x with bounded ±0.25px
phases and thresholds 120/128/136. BOX downsampling produces the same packed
1bpp format. Font size, baseline, advance and line boxes are unchanged.
ASCII retains the previous topology-protected 4x policy; 12px ASCII retains
Pillow 4x unchanged. Reader body and out-of-subset fallback fonts are unchanged.

Selection starts from the previous unhinted 4x/native-fallback glyph. It accepts
only candidates that do not worsen continuous coverage blur error, absolute ink
mass error, ink/core/local-darkness centroid drift, or strong-extra pixel count
(black pixels where outline coverage is ≤0.1). The full 16x outline also supplies
stroke medial axes, resolvable white medial axes (radius ≥0.5 native pixels),
and actual enclosed holes. Stroke-axis distance, the fraction of white axes
blocked by ≥0.5 native pixels, and lost/extra hole counts cannot worsen. Average
white-axis distance admits at most half a reference sample (1/32 native pixel
at 16x) of change: this declared finite-position precision prevents negligible
subpixel changes from forbidding restoration of an entire thin stroke. Raw
white-axis distance changes are still reported, not presented as improvements.
Hole overlap uses maximum one-to-one matching so a merge cannot satisfy two
reference holes. Missing white-axis samples remain undefined, not perfect zeros.
The existing native component/hole-count protection remains in place. Among admissible candidates,
selection prioritizes strong extras, then worst centroid drift, then coverage.
If no candidate meets all constraints, the glyph stays unchanged. Count
protection does not prove hole identity or semantic stroke preservation; central
ink is a Gaussian geometric proxy for 中宫, not an expert-defined semantic core.
The outline-distance search uses an expanding nearest-center search with an
exact pixel-square distance bound, tested against exhaustive search. This is a
conservative, finite-precision engineering improvement, not a globally optimal
or calibrated readability score. Other diagnostics can still trade off.
Runtime remains 1bpp and
does not add refreshes, dithering, heap allocations or vector font parsing.
Line boxes include four extra pixels (six for 22px) for descenders; regular/bold 18px faces
share a baseline. Generation rejects vertically clipped catalog glyphs.
OTF source files are not embedded. Arbitrary names outside the subset retain
the existing Unicode fallback scaled into the requested line box; they are not
claimed to have native Noto rendering. Reader body typography is unchanged.

Regenerate with `uv run tools/generate-editorial-font.py --source build-font-sources`.
The local JSON report lists the policy, native control, per-CJK before/after
metrics and packed masks, unchanged cases and fallback counts;
verify it with `uv run tools/editorial_font_test.py build-font-sources/editorial-optimization.json`.
The earlier generator's 4x binary reference was also a candidate, favoring itself.
Neither its old score nor the new geometric proxy proves real-panel readability.
Place upstream `NotoSansCJKsc-Regular.otf` and `NotoSansCJKsc-Bold.otf` from
`Sans2.004/Sans/OTF/SimplifiedChinese/` there first. The generated header is
committed, so ordinary firmware builds require neither Pillow nor font downloads.

### Reproduce the optimization comparison

`uv run tools/compare-cjk-optimization.py --report build-font-sources/editorial-optimization.json`
generates static before/after figures, per-character CSV and separate statistical
summaries in `build-font-sources/cjk-optimized/`. These include P95/P99 tails,
covariance, central-white/ink windows, extra/missing pixel attribution, four
stroke directions, Fourier phase/power, catalog-frequency weighting and paired
glyph-resampling intervals. No new runtime processing or release gate is added.
`uv run tools/cjk_balance_test.py` verifies continuous thin-stroke handling,
exact centroid attribution, Fourier translation behavior and undefined tilt.

For a full post-generation diagnostic without rerunning unchanged controls:
`uv run tools/evaluate-cjk-font.py --source build-font-sources --methods embedded --output build-font-sources/cjk-post-optimization --pages build-ui-preview`.
Regenerate previews first with `bun tools/ui-preview.ts --docs`; do not combine
old screenshots with a new embedded font. Omit `--methods` for all seven controls.
These reports do not measure real-panel optics, recognition, weight PSE or power.
Compare a retained pre-change audit with the current audit using
`uv run tools/compare-cjk-optimization.py --diagnostics <before-report.json> <after-report.json>`.
This writes matched basic-metric CSV and `independent-diagnostics.md`, including
binary-reference regressions. A binary reference may already omit real thin
strokes; compare those results with the full-outline diagnostics, not a single
score. The selected font is not claimed to dominate every evaluated metric.

### Independent CJK diagnostics

Run `uv run tools/evaluate-cjk-font.py --source build-font-sources` to generate
`build-font-sources/cjk-evaluation/report.json` and `comparison.png` locally.
This does not modify the firmware font. It compares native monochrome hinting,
forced CJK autohinting, native grayscale thresholding, unhinted 2x/4x coverage,
the Pillow 4x control, and the **actual embedded font** (decoded from the header,
including topology fallbacks) against separate **16x unhinted outline coverage**.
The reference is independent of the candidates, not human ground truth.
By default all seven firmware roles are evaluated using the complete UI catalog
plus stress characters; Micro remains ASCII only. `--sample-only` runs a smaller
experiment, and `--output` selects a separate output directory.

Metrics remain separate: blurred coverage error, ink area, skeleton overlap,
component/hole counts, median stroke width, and interior white-channel loss.
Missing/spurious centerlines and lost/extra enclosed holes distinguish thinning
from thickening; white-channel contraction/expansion are reported separately.
Ink height/bottom errors and per-glyph side bearings expose alignment changes.
Distance-transform width P5/P95 values are geometric descriptors, not calibrated
physical stroke widths. Hole overlap is only an approximation of hole identity.
Each reports mean, P95 and maximum, with separate dense-character, punctuation
and confusable-character groups. 未/末, 土/士, 己/已/巳 are compared at three
blur strengths; identical bitmaps are reported explicitly. Larger pair distances
are not automatically better: a malformed glyph can create spurious differences.
Separate UI CJK and ASCII groups prevent ASCII characters from hiding CJK errors.
Catalog byte counts exclude added stress characters and include 12 bytes per
index record; lossless row-trimming savings are reported separately. These are
font data estimates, not measurements of linked firmware size or decoding speed.
An exploratory frontier keeps byte cost, mean/P95 blur error and P95 gap,
topology and skeleton losses separate. It is not a certified readability optimum.
The nearest-neighbor comparison shows output pixels at 3x scale.
`summary.md` gives a readable overview. Neutral-ID `panel-T*.png` / `.pbm` pages
are native 300x400 or 400x300, with 1/2/3-pixel bars, gaps and diagonals. Canonical
P4 PBMs work with the existing `tools/edge-page-server.ts`; this tool does not
start a service, alter device settings or send pages to the device. `report.json`
contains trial IDs and method mappings: keep it hidden during a blind comparison.
The supplied order is only an example; randomize matched role/orientation blocks
per participant. These calibration pages are not timed recognition trials.

Gaussian blur is a sensitivity experiment, not calibrated panel optics or a
ghosting simulation. Skeleton/count metrics can miss structural errors; Chinese
stress characters do not establish coverage for all CJK scripts. No combined
readability score, automatic replacement, or quality gate is imposed. Confirm
shortlisted candidates on the real panel before changing firmware typography.
Synthetic checks: `uv run tools/cjk_font_test.py`.
Validate a completed report by supplying its path to that test.
Run `bun tools/cjk_page_test.ts <output-directory>` to verify all generated
PBMs through the existing remote-page encoder, including every pixel's polarity
and portrait row padding. Reformat an existing report without rendering again
with `uv run tools/evaluate-cjk-font.py --summarize-report <report.json>`.
Use `--enrich-report <report.json>` to update corpus/context associations without
rerasterizing glyphs; it does not update original glyph measurements. Keep font
sources and the preview sources matched when interpreting those associations.

For real-panel calibration, fix camera position, exposure and lighting; align
photos to the pixel grid and compare full refresh, repeated partial refresh and
black-to-white transitions. Estimate edge spread, contrast and residual darkness
only after accounting for camera blur. Recognition accuracy/time and energy
(`integral V*I dt`) require separate experiments; no values are synthesized here.

### Quantitative weight and context analysis

The evaluator is split into rasterization (`cjk_raster.py`), independent glyph
metrics (`cjk_metrics.py`), corpus/page context (`cjk_context.py`), and optional
real measurements (`cjk_measurements.py`). All run offline; none selects or
rewrites firmware fonts. No scalar quality score or new release gate is added.

Additional per-glyph results include:

- MSE/PSNR, symmetric boundary P95/max, signed-distance error, SSIM, adaptive
  native-size MS-SSIM and FSIM using phasepack phase congruency. MS-SSIM stops
  before a level becomes smaller than a 3px window and renormalizes the retained
  scale weights; it is not the standard five-scale score. FSIM's phasepack PC
  implementation differs from the original MATLAB implementation. Neither is
  validated as a tiny-glyph readability predictor.
- Ink density, area/skeleton-length effective width, height-normalized width,
  local black concentration, complexity and regional blackness. Complex CJK
  glyphs must not be forced to have the same ink area as simple Latin glyphs.
- Horizontal, vertical and both diagonal stroke widths from normal rays at
  locally straight skeleton points; branch/end neighborhoods are excluded.
  Width distributions, variation and local thickening ratios are reported.
  Known 1–4px horizontal/vertical bars verify the pixel-cell convention; diagonal
  measurements still have quantization error. The older 2×EDT values remain
  available as explicitly uncalibrated descriptors.
- Centroid shift, second moments, principal-axis drift (only for sufficiently
  anisotropic glyphs), splits/merges, missing-stroke patches and cubical coverage
  persistence bottleneck distances. Cubical connectivity is distinct from the
  generator's existing foreground/background topology protection.
- Nearest neighbors across the entire evaluated glyph set, reference margins,
  distinctive-region errors and exact collapses. These are model distances, not
  human confusion probabilities. Blur, contrast, subpixel x/y phase and synthetic
  previous-image residue curves remain separate, uncalibrated scenarios.
- Actual raw/row-trim/RLE/zlib byte sizes, current index overhead, host decode
  time and traced peak memory. Whole-face codecs sacrifice current random access;
  their host costs are not ESP32 costs or linked firmware sizes.

Each numeric metric has unweighted and catalog-frequency-weighted statistics.
Signed errors retain both tails and absolute P95 rather than cancelling
thickening and thinning into an apparently good mean. Byte and next-byte
conditional entropy estimates are reported alongside actual codec sizes, not
mistaken for attainable sizes or rigorous source-entropy bounds.
Catalog parsing preserves language/string boundaries and removes formatting
placeholders. Counts, adjacency, document presence, Jaccard, conditional
cooccurrence, lift and PMI remain separate; low-support pairs are flagged, not
discarded. Source frequency is not measured screen exposure or reading time.
Frontiers use the common candidate character intersection so absent embedded
stress glyphs cannot make a candidate appear better. Full embedded storage is
reported separately from a sample's evaluated storage.

For real layout context:

```sh
bun tools/ui-preview.ts
uv run tools/evaluate-cjk-font.py --source build-font-sources \
  --pages build-ui-preview --output build-font-sources/cjk-quantitative
uv run tools/cjk_quant_test.py build-font-sources/cjk-quantitative/report.json
```

Preview tests compile `NOTE4_FONT_TRACE` only into their native executable.
Its fixed-capacity paint records do not allocate during drawing; no tracing is
compiled into firmware. Sidecar `.text.json` files identify actual role, font
source, style, position, clipping, inversion and text run. Reader/fallback source
rows are exported and resampled with the same SDK style rules. Completely
occluded paint events are excluded; partial ink retention remains visible.
Reports include same-page/run/Latin-word-or-CJK-run graphs, spacing proxies,
adjacent weight jumps and CJK/Latin ratios **within matched roles and styles**.
Normal/bold hierarchy is not automatically treated as an error. Fixtures have
no assigned usage probability; CJK runs are not dictionary-segmented words.
`--roles Caption Micro --sample-only` provides a smaller development run.

`--measurements local-measurements.json` accepts optional arrays:

- `recognition`: `participant`, `candidate`, `role`, `expected`, `answer`,
  `time_ms`. Reports accuracy, correct/all response times, confusion matrices,
  empirical mutual information and participant-cluster bootstrap accuracy CIs.
  Empirical MI is finite-sample biased; one participant has no population CI.
- `weight_judgments`: `participant`, `candidate`, `role`, `weight_delta`,
  boolean `test_heavier`. Fits a separate logistic PSE only if the observed
  range brackets the 50% crossover; not a readability estimate.
- `energy`: `id`, `measurement_boundary`, `samples` with strictly increasing
  `time_s`, `voltage_v`, `current_a`. Integrates measured V×I using trapezoids.
- `refreshes`: actual diagnostic records, optionally `busy_us`, `spi_bytes`,
  `ram_bytes`, `waveform_triggers`. These counters are not converted into power.
- `optical`: `id`, `capture`, `target`, `black_level`, `white_level`,
  `registration`, optional `previous` and `edge_roi` `[x,y,w,h]`. Image paths
  resolve relative to the JSON file. Captures must already be registered to the
  native pixel grid and black/white calibrated. Outputs include native-pixel
  ESF/LSF/MTF proxies and previous-image residual projection; camera and panel
  blur are not separated and this is not ISO slanted-edge MTF.

Missing measurements use `needs_measurement`/null, never a fabricated zero.
DISTS uses optional `--dists-model` with a **trusted local pretrained TorchScript
model** accepting two RGB float tensors and returning one distance. Install its
runtime through `uv run --with torch tools/evaluate-cjk-font.py ...`; export the
author's trained VGG/alpha/beta weights beforehand. No weights are downloaded,
untrained network scores are not substituted, and missing weights use
`needs_model`. Native glyphs receive a white surround, never image enlargement.
The local model's provenance/training must be verified by its provider; loading
TorchScript executes code. DISTS scores are not human legibility measurements.

Synthetic checks: `uv run tools/cjk_quant_test.py`. They cover width calibration,
centroid translation, erased/split strokes, persistence, collapsed neighbors,
corpus denominators, source-mask reproduction, codecs and data ingestion.
The image metrics follow [SSIM](https://www.cns.nyu.edu/~lcv/ssim/index.html),
[FSIM](https://research.polyu.edu.hk/en/publications/fsim-a-feature-similarity-index-for-image-quality-assessment/),
[Kovesi phase congruency](https://github.com/alimuldal/phasepack) and
[DISTS](https://github.com/dingkeyan93/DISTS); the adapted implementations and
tiny-raster limitations above are intentional, not claims of equivalence.

## Legacy body and reader fallback

The existing proportional 16px ASCII font remains the system face. Chinese UI text uses
GNU Unifont 15.1.05 at its native 16px size. When Reader is enabled, UI rendering
reuses its existing glyph data. Without Reader, `ui_chinese_font.h`
contains only non-ASCII characters from the system string catalog, with sorted
Unicode indices and fixed bitmap records. Disabling Chinese UI and Reader
excludes that data entirely.

The subset retains the reader font's [copyright notice and source](../../note4_reader/font/README.md)
and [SIL Open Font License 1.1](../../note4_reader/font/OFL-1.1.txt).
No upstream UI strings or artwork are copied.

After editing Chinese strings, regenerate with:

```bash
uv run --no-project tools/generate-ui-font.py
```
