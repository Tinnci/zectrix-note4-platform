# Reader bitmap font

`reader_font.bin` is an independently generated 16px subset of GNU Unifont
15.1.05. This project distributes the font data under
[SIL Open Font License 1.1](OFL-1.1.txt), one of the upstream font's two offered
licenses. Application code remains under the project MIT license.

The upstream BDF copyright notice is:

Copyright (C) 1998-2024 Roman Czyborra, Paul Hardy, Qianqian Fang, Andrew Miller,
Johnnie Weaver, David Corbett, Nils Moskopp, Rebecca Bettencourt, Ho-Seok Ee,
et al.

Source: [GNU Unifont 15.1.05 distribution](https://unifoundry.com/pub/unifont/unifont-15.1.05/).
The upstream `COPYING` and BDF header explicitly offer OFL 1.1 for font files.
The subset contains 40,181 glyph slots. It covers Latin, punctuation, kana,
CJK Extension A/unified ideographs, Hangul syllables and fullwidth forms.
The renderer scales the same glyphs to 24px without a second bitmap allocation.

S1.2 replaces the 1,325,973-byte raw bitmap array with 824,959 bytes of shared
8x8 tiles. All widths and pixels are unchanged. This private build asset has:

| Region | Bytes | Representation |
| --- | ---: | --- |
| Widths | 5,023 | One bit per glyph, low bit first. Zero means 8px, one means 16px |
| Glyph indices | 321,448 | Four little-endian uint16 tile IDs per glyph: top left/right, bottom left/right |
| Tile dictionary | 498,488 | 62,311 distinct 8x8 tiles, eight row bytes each, high bit on the left |

Glyph order follows `RANGES` in the generator and reader. Uncovered codepoints
use U+FFFD. Tile IDs follow first occurrence, so generation is deterministic.
`GlyphBitmap()` returns an immutable view into these tiles. `GlyphWidth()`
reads the width bit without reconstructing pixels. No decompression buffer,
mutable cache or external asset partition is needed.

To regenerate from the upstream BDF:

```bash
uv run --no-project tools/generate-reader-font.py unifont-15.1.05.bdf.gz reader_font.bin
```

Use `--raw-output reference.raw` to also produce the original width-plus-bitmap
records. Compare every production glyph and row with those records:

```bash
ZECTRIX_FONT_REFERENCE=/absolute/path/reference.raw bash tools/test-reader-font.sh
```

The regular Host test also generates an independent BDF fixture and
checks all glyph slots, tile bounds, widths and view lifetimes.
