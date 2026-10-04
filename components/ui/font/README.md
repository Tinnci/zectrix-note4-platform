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
advance. A per-face offline search compares native/2x/4x coverage and thresholds
112/128/144, choosing minimum geometric distortion on the storage/distortion
Pareto frontier. Candidates with more topology errors than native fall back to
native for that glyph. This is an uncalibrated proxy, not a claim of universally
optimal readability, energy or gray-level accuracy. Runtime remains 1bpp and
does not add refreshes, dithering, heap allocations or vector font parsing.
Line boxes include four extra pixels (six for 22px) for descenders; regular/bold 18px faces
share a baseline. Generation rejects vertically clipped catalog glyphs.
OTF source files are not embedded. Arbitrary names outside the subset retain
the existing Unicode fallback scaled into the requested line box; they are not
claimed to have native Noto rendering. Reader body typography is unchanged.

Regenerate with `uv run tools/generate-editorial-font.py --source build-font-sources`.
The local JSON report lists every candidate and selected frontier point;
verify it with `uv run tools/editorial_font_test.py build-font-sources/editorial-optimization.json`.
Place upstream `NotoSansCJKsc-Regular.otf` and `NotoSansCJKsc-Bold.otf` from
`Sans2.004/Sans/OTF/SimplifiedChinese/` there first. The generated header is
committed, so ordinary firmware builds require neither Pillow nor font downloads.

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
