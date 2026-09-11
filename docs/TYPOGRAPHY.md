# Algorithmic text styles

R1.3 adds emphasis, secondary text and physical-key labels using the existing
1bpp canvas and bitmap fonts. Native styling introduces no font face, glyph
cache, canvas, background task or allocation call. System headings and Continue
Reading use emphasis; Home's year uses secondary ink. Calculator and Flashcards
demonstrate styled copied Lua frames.

## Style API

SDK 1.2 adds the one-byte `zectrix::sdk::TextStyle` enum and constexpr `|`, `&`
and `HasStyle` helpers. Application lifecycle interfaces remain source
compatible. The vocabulary lives in the dependency-free `zectrix_text`
component, shared by Reader, UI, the SDK umbrella and the optional runtime.
The SDK still does not expose a canvas, hardware handle or binary ABI.

```cpp
using zectrix::sdk::TextStyle;
const auto style = TextStyle::Bold | TextStyle::Italic;
const int width = canvas.TextWidth("Reading", 1, style);
const int height = canvas.TextHeight("Reading", 1, style);
canvas.Text(12, 64, "Reading", 1, false, style);
canvas.TextFitted(12, 96, title, 376, false, style);
canvas.Text(12, 132, "OK", 1, false, TextStyle::Keycap);
```

The optional last style argument defaults to Regular for `Text`,
`TextCentered`, `TextFitted`, `TextWidth`, Reader `GlyphWidth` and `DrawGlyph`.
Coordinates name the top-left of the measured box. Canvas scales are integers
1–16; null/empty strings and invalid scales have zero extent and draw nothing.
`TextHeight` includes decorations. `TextFitted` reserves a complete styled
ellipsis and, for Keycap, one outer frame; it never splits a Unicode scalar.
Inverted Canvas text fills the measured box before painting white ink.
Reader glyph drawing remains transparent on either background.

| Style | Geometry and ink |
| --- | --- |
| Regular `0` | Existing glyph widths, pixels and scaling |
| Bold `1` | Narrow glyphs expand right by `height / 16` device pixels: 1px at 16/24px |
| Italic `2` | Narrow glyphs shear right from 0px at the bottom to `height / 8` at the top: 2/3px at 16/24px |
| Dim `4` | Screen-anchored 4×4 Bayer mask retains 75% of foreground samples, with ordinary 1bpp refresh |
| Underline `8` | One-pixel rule one pixel below the glyph; measured height grows by 2px |
| Keycap `16` | One frame around a Canvas text run; horizontal padding is 3px per side, vertical padding 2px, multiplied by scale |

Styles combine. Spaces retain their normal advance. Wide bitmaps and CJK
codepoints express Bold/Italic through Underline: their strokes are neither
expanded nor sheared. This protects one-pixel gaps in 16px Chinese/Japanese/Korean
text and the same gaps after 24px scaling. It is an emphasis fallback, not
another font weight. Explicit Underline combines without adding another rule.
Dim affects ink, including underlines, on either background; keycap borders
stay solid for recognition.

`MeasureGlyph` is the single geometry calculation used by Reader pagination,
Canvas measurement and both bitmap painters. Advance includes all horizontal
expansion, so glyph ink never overhangs the next cell. Reader `GlyphHeight`
includes its underline. Glyph-only helpers do not draw Keycap, which needs the
whole run; the bounded UI `DrawUtf8Line` helper routes Keycap labels through
Canvas's UI font and fitting rules.

Clipping precedes raster work and uses widened coordinates. A clipped redraw
has the same Bayer phase as a complete draw. Micro-app rendering intersects
the guest region with the caller's clip and restores that clip afterwards.
These changes use the existing foreground owner, ViewPortScheduler and
DisplayService; they do not select waveforms or add grayscale refreshes.

## Streamed EPUB emphasis

The existing byte-at-a-time XHTML decoder preserves these semantic tags:

| XHTML | Style |
| --- | --- |
| `b`, `strong`, `h1`–`h6` | Bold |
| `i`, `em` | Italic |
| `u` | Underline |
| `small` | Dim |

Tags can nest, including namespace-qualified names. Attributes and CSS do not
change styles. Hidden head/script/style/SVG/ruby annotations remain hidden;
self-closing style tags have no lasting effect. The decoder holds at most 16
styled tag IDs. Deeper nesting inherits the current style and counts supported
closing tags back out; mismatched supported closing tags unwind to the nearest
matching entry. Unmatched closings are ignored. An unclosed style lasts to the
chapter end, where decoder state resets. Recovery is bounded and does not
build or validate a DOM.

Each emitted token owns its style. Each page glyph still occupies eight bytes,
using 24 bits for the Unicode scalar and eight for style. The two 640-glyph
page buffers retain their original size. A buffered line keeps the style of
its text even after later closing tags have been decoded.

Regular line advances remain 20px/32px. If an underline cannot fit at the bottom
of the 216px body, the whole pending line moves to the next page. This can
reduce the number of rows on a decorated page. Wrapping, fitting and painting
account for styled width/height, including long words and mixed scripts.
Source-byte anchors, stored bookmark format and synchronization payloads do not
change. EPUB seeking replays the containing chapter to reconstruct nesting;
Next, Previous and font reflow use the same path and remain cancellable.

TXT remains literal, including `**stars**`, `_underscores_` and angle brackets.
This iteration chooses existing EPUB semantic markup over a second Markdown
parser. Implicit Markdown would change ordinary text and require additional
delimiter/context recovery for TXT's near-offset resume. Full CSS, Markdown
documents, images, links, embedded fonts, hollow headings and complex-script
shaping are outside this implementation.

## Micro-apps

The optional fifth argument to `note4.text(x, y, text, scale, style)` uses
`note4.REGULAR`, `BOLD`, `ITALIC`, `DIM`, `UNDERLINE` and `KEYCAP`. Lua's `|`
combines flags. Old four-argument scripts retain Regular behavior; unsupported
bits and negative values fail through the existing drawing-error cleanup.
The completed native frame owns the style with its text, so presentation
retries neither borrow guest memory nor execute the guest again.

```lua
note4.text(12, 20, "Question", 1, note4.BOLD)
note4.text(12, 56, "Answer", 1, note4.ITALIC)
note4.text(12, 96, "OK: reveal", 1, note4.KEYCAP)
```

The existing 64-command/2,048-text-byte quotas, 128-byte call limit, 1×/2×
scale limit and VM execution/heap limits remain. Each command grows from 12
to 14 bytes, adding 128 bytes to an owned frame without another allocation.
The additional Lua constant names are installed once per VM within its existing
heap quota; native measurement and rasterization allocate nothing.

## Verification and cost

```bash
bash tools/test-host.sh
ZECTRIX_LOCALIZATION_SANITIZE=1 bash tools/test-localization.sh
ZECTRIX_READER_SANITIZE=1 bash tools/test-reader.sh
ZECTRIX_RUNTIME_SANITIZE=1 bash tools/test-runtime.sh
mkdir -p build-typography/previews
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-typography/previews" bash tools/test-display-service.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

All 39 Host targets and the Reader, localization and runtime ASan/UBSan suites
passed. Tests exercise every style combination, three font compositions,
normal/inverted text, clipping, ellipses, extreme coordinates and zero-allocation
Canvas drawing. Reader checks compare 5,000 styled glyphs across 16 small-font
and 38 large-font pages, replay every anchor, reflow both ways, navigate back,
exercise bottom-line decoration, nesting overflow, malformed UTF-8, entities
and literal TXT. Its 2 MiB TXT tail resume still reads 63 source bytes. UI tests
render actual styled EPUB pages and preserve status/content ownership. Lua
tests exercise copied styles, invalid flags, clipping and existing quota faults.
English/Chinese UI, 16px/24px reader pages and both pilot previews were inspected.

Measured ESP32-S3 builds with production `-Os`:

| Metric | Full | Minimal |
| --- | ---: | ---: |
| Firmware bytes | 2,486,256 | 516,528 |
| Increase from S1.2 | 4,048 | 2,000 |
| Static internal RAM bytes | 201,815 | 108,315 |
| Reader font bytes | 824,959 | 0 |

Static internal RAM and font assets are unchanged. Full retains 659,472 bytes
(21.0%) in each existing 3 MiB slot. No partition migration or hardware flash
was needed. Physical panel readability and refresh timing remain to be measured
on hardware; the visual checks above use Host-rendered monochrome images.

## References

CrossPoint's [EpubReaderActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp)
informs bounded page work and position preservation. Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
inform deferred ownership and clipped rendering. Font transformations and
semantic-tag handling here are independent implementations. Existing transfer
and sleep-cover behavior remains described in [READER.md](READER.md).
