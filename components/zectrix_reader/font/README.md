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
The subset contains 40,181 glyph slots, each with a width byte and sixteen
big-endian bitmap rows, totaling 1,325,973 bytes. It covers Latin, punctuation,
kana, CJK Extension A/unified ideographs, Hangul syllables and fullwidth forms.
The renderer scales the same glyphs to 24px without a second bitmap allocation.

To regenerate from the upstream BDF:

```bash
uv run --no-project tools/generate-reader-font.py unifont-15.1.05.bdf.gz reader_font.bin
```
