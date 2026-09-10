# System languages

E1.7 adds Simplified Chinese and English to the Note4 system UI. A new Full
installation starts in Chinese. Open **设置 / SETTINGS**, then **语言 / LANGUAGE**
to choose a language. The picker always names the choices **English** and
**简体中文**, so either language can be recognized before applying it.

UP/DOWN moves the selection; OK applies and saves it. Hold OK returns one
scene without applying an unconfirmed choice. The Settings parent retains its
selected row. Automatic showcase is a separate row: OK toggles and saves it,
and a failed save can be retried with OK. Hold DOWN retains global shutdown.

The language changes immediately, including the status bar, and a Quality
frame replaces the previous language. A failed display commit retries on idle.
If saving fails, the choice remains active for this boot and the picker shows
that it was not saved; OK retries the write. These operations use the existing
foreground owner, deferred SceneManager and DisplayService refresh policy.

## Storage and composition

`StorageService` stores `ui.language` as an unsigned 32-bit integer: `0` is
English and `1` is Simplified Chinese. Boot restores it before the splash and
Home are drawn. Missing, invalid or excluded language values use the compiled
default without rewriting storage. In particular, loading a Chinese preference
on an English-only build leaves the preference available for a later Full build.
No language change modifies book contents, source-byte bookmarks or sync keys.

`zectrix_locale.h` exposes `Text` identifiers and `Tr()` for system copy.
`zectrix_strings.inc` defines 339 pairs of translations beside their identifiers; static
pointer tables provide direct lookup and an English fallback. Only the language
value is persisted. Text IDs can move as copy is added. Queries return immutable
strings and never allocate, read storage or build a dictionary during drawing.

Application presentation metadata carries an optional `Text` ID. The original
SDK descriptor name and application ID remain available; the Launcher resolves
the display text when drawing. External entries with no translation ID use
their supplied label. A language change therefore keeps application targets,
module selection, focus and the scene stack intact.

## Fonts and build choices

| Configuration | System languages | Font source |
| --- | --- | --- |
| Full | Chinese and English; Chinese default | Existing reader glyph data plus the proportional ASCII face |
| Reader off, `ZECTRIX_ENABLE_UI_CHINESE=y` | Chinese and English | 355 system glyphs: 12,425 bytes including Unicode indices |
| Minimal | English | Existing ASCII face; both Chinese font sources are excluded |

`ZECTRIX_ENABLE_UI_CHINESE` controls the Chinese strings and the small UI font.
`ZECTRIX_UI_DEFAULT_CHINESE` selects the default when the pack is available.
The two options live in **Zectrix modules**. Reader remains independently
selectable: it supplies broad CJK coverage for book text and dynamic titles.
With only the UI subset, characters outside the system catalog use a fallback
glyph. English-only builds also replace unsupported Unicode scalars once per
character rather than once per UTF-8 byte.

The canvas shares one UTF-8 decoder for measuring and drawing. It retains the
existing ASCII metrics and uses native 16px Chinese glyphs, including selected
white-on-black text. Fitted text stops at scalar boundaries, reserves space for
an ellipsis and stays inside its requested width. Titles, tile labels and
physical-key hints retain their existing 400 x 300 layout bounds. Reader body
glyphs and its 20/32px line advances keep their existing streaming layout.

When changing Chinese copy, run
`uv run --no-project tools/generate-ui-font.py`. The generator extracts the
required characters from the committed GNU Unifont reader data; see the
[font notice](../components/zectrix_demo_ui/font/README.md). Normal firmware
builds use the committed subset and need no font download or runtime decoder.

Localized surfaces include Home, status indicators, Settings, Clock, Reader,
Send Books, Connectivity, sleep covers and the system menus for tools and
diagnostics. Hardware diagnostic measurements, protocol names, book/SSID data,
CLI output and legal identifiers retain their source representation. The
browser transfer page and Android Companion have their own UI resources.

## References and verification

CrossPoint's [I18n](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/lib/I18n/I18n.cpp)
uses string IDs, immutable language data and English fallback. Its
[LanguageSelectActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/settings/LanguageSelectActivity.cpp)
selects the current language on entry and saves an explicit selection. Note4's
two-language implementation uses direct static tables and the foreground
display owner. Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
inform the private picker and coordinated content/status refresh. The existing
[streamed reader and transfer ownership](FIRMWARE_UI_STUDY.md) remain in use.

```bash
bash tools/test-host.sh
ZECTRIX_LOCALIZATION_SANITIZE=1 bash tools/test-localization.sh
mkdir -p build-ui-e1-7/previews
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-ui-e1-7/previews" bash tools/test-display-service.sh
```

The localization suite runs Full, Chinese without Reader and English-only
combinations. It checks format-argument consistency, catalog glyph coverage,
footer widths, malformed UTF-8, clipped text, zero-allocation lookup/drawing,
restart restoration, failed-commit retries and picker navigation. Display
integration runs both languages, including simultaneous status/content changes,
refresh recovery and an entirely white final privacy cover. PBM inspection
artifacts use `zh-` for Chinese.

E1.7 passed all 34 Host targets, the three localization ASan/UBSan compositions,
ShellCheck and ESP-IDF 5.5.2 builds for Full, Minimal and Chinese without Reader.
Rendered Chinese and English Home, settings, reading, transfer, connectivity
and sleep screens were inspected, including failed-save hints and privacy output.

| Firmware composition | Application bytes | Static internal RAM bytes |
| --- | ---: | ---: |
| Full | 3,018,304 | 213,575 |
| Minimal | 562,752 | 118,731 |
| Minimal with Chinese UI | 584,256 | 118,739 |

Enabling Chinese on Minimal adds 21,504 firmware bytes and 8 static RAM bytes.
The linked images contain the reader font, no Chinese font, and only the UI
subset respectively. Full/Minimal keep the same book and OTA partitions and
boot protection. Device smoke and physical panel readability measurements were
not performed for this UI iteration.
