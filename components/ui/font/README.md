# System fonts

The existing proportional 16px ASCII font remains the system face. Chinese UI text uses
GNU Unifont 15.1.05 at its native 16px size. When Reader is enabled, UI rendering
reuses its existing glyph data. Without Reader, `zectrix_ui_chinese_font.h`
contains only non-ASCII characters from the system string catalog, with sorted
Unicode indices and fixed bitmap records. Disabling Chinese UI and Reader
excludes that data entirely.

The subset retains the reader font's [copyright notice and source](../../zectrix_reader/font/README.md)
and [SIL Open Font License 1.1](../../zectrix_reader/font/OFL-1.1.txt).
No upstream UI strings or artwork are copied.

After editing Chinese strings, regenerate with:

```bash
uv run --no-project tools/generate-ui-font.py
```
