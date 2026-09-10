# Firmware forks and UI architecture study

E1.1 recommends a reading-first home screen, a separate tools list and static
sleep covers for Note4. The existing application runtime, bounded scenes and
single display owner already support this direction. The first delivered
change is an overflow indicator for the Launcher. The larger home, font and
cover changes below are proposals for subsequent iterations.

E1.2 now implements the [Home dashboard](HOME.md), including local Continue
Reading, catalog-derived tiles and Tools. This study's source observations
and E1.1 measurements remain a record of that iteration; the remaining font
and imported-cover proposals are still future work.

## Sources and scope

Source review date: **2026-09-10**. This study inspected application and renderer
source in the branches below. Links follow upstream branches and can change.
Upstream source observations are separate from Note4 build measurements.
No upstream firmware was built or run on Note4.

| Project | Repository and inspected branch | Root license | Relevant implementation |
| --- | --- | --- | --- |
| CrossPoint | [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader), `develop` | MIT | Recent books, cooperative section pagination, sleep composition and streamed web uploads |
| Biscuit | [yattsu/biscuit](https://github.com/yattsu/biscuit), `master` | MIT | Eight-category dashboard and category-local app lists |
| CrossMux | [0x1abin/crossmux](https://github.com/0x1abin/crossmux), `main` | MIT | Reading home with Apps, Standby faces and AirPage image delivery |
| CrossInk | [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk), `main` | MIT | Minimal/Dashboard themes, progress overlays and page font preparation |
| Flipper Zero | [flipperdevices/flipperzero-firmware](https://github.com/flipperdevices/flipperzero-firmware), `dev` | GPL-3.0 | Scene handler tables and viewport invalidation |
| Momentum | [Next-Flip/Momentum-Firmware](https://github.com/Next-Flip/Momentum-Firmware), `dev` | GPL-3.0 | Menu styles, desktop keybindings and asset packs |
| Unleashed | [DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware), `dev` | GPL-3.0 | Desktop clock viewport and configurable favorite applications |

The Biscuit entry is the CrossPoint-derived Xteink firmware. Other projects
with the same name are unrelated. Root licenses do not establish the license
of each bundled font or image. This iteration uses original Note4 code and
existing assets. It does not import upstream code, fonts, artwork or themes.

## Home screens and everyday navigation

**CrossPoint** puts recent reading ahead of File Browser, Recents, File Transfer
and Settings. Its [HomeActivity][cp-home] skips missing recent files and loads
cover thumbnails when needed. This supports a direct Continue Reading action
with an explicit library fallback. Note4 can initially show a title and saved
progress without decoding an EPUB cover during boot.

**Biscuit** moves toward a general pocket tool. Its actual
[`goHome()` route][biscuit-manager] opens `AppsMenuActivity`, rather than the
inherited reader-oriented `HomeActivity`. The [dashboard][biscuit-apps] has
eight categories, with Reader one of them. It supports grid and Radar
navigation, system information and category lists with descriptions. This
demonstrates how grouping prevents an ever-growing top-level menu. Note4 has
three buttons and a 400 x 300 display, so a vertical tools list is a better
initial fit than a spatial dashboard. Heap and uptime remain in Device Info.

**CrossMux** keeps [recent books on Home][mux-home] and adds an Apps destination.
Its [app-entry table][mux-apps] combines identity, label, icon and opening
operation, with availability filtering. Note4's S1.3 catalog already supplies
both labels and runtime destinations. A future Tools scene should filter that
catalog instead of creating a second label-to-index switch.

**CrossInk** explores two useful styles. [Minimal][ink-minimal] uses a prominent
recent cover, a quiet empty state and compact lists. Its file list derives row
height from font metrics and shows a scrollbar only for multiple pages.
[Dashboard][ink-dashboard] combines a cover with reading statistics and handles
missing artwork with text/icon content. On Note4, one recent-reading summary
and clear actions will remain legible at the available resolution. A dense
statistics column needs a separate screen or a larger layout.

**Momentum** implements [List, Compact, CoverFlow and other menu styles][momentum-menu]
with scroll indicators. [Desktop keybindings][momentum-keys] distinguish short
and long presses and map them to named destinations. Its [asset packs][momentum-assets]
replace icons and animations through SD content. The useful lessons are
visible focus, stable destinations and predictable shortcuts. Note4's default
home should remain still when idle. Animation timers and runtime theme packs
would add refresh and memory costs without improving basic navigation.

**Unleashed** provides [configurable favorites][unleashed-favorites] and a
[desktop clock viewport][unleashed-desktop]. Its clock timer samples once per
second but invalidates the viewport only when the displayed hour, minute or
time format changes. Note4 already applies this visible-change rule to its
status bar. A future favorite must use an ApplicationId and handle a disabled
module or removed destination. Menu indices cannot serve as persistent IDs.

All proposed Note4 layouts retain UP/DOWN selection, release-OK confirmation,
hold-OK Back and global hold-DOWN shutdown. A new shortcut must not consume the
existing return or shutdown gestures.

## Reading, sleep covers and local transfer

CrossPoint's [EPUB activity][cp-reader] loads section pages, builds more pages
in bounded foreground slices and conditionally prepares the next page's
fonts during idle time. It also has section caches and heap checks. Note4
adopts the cooperative work pattern through its existing `Engine::Poll`,
source anchors and cancellation points. A complete chapter cache or a
whole-book page count is not required for Continue Reading. The current EPUB
limitations in [READER.md](READER.md) still apply, including no CSS or images.

CrossPoint's [SleepActivity][cp-sleep] supports book covers, custom images,
blank screens and transparent overlays, with fallbacks when cover generation
or loading fails. CrossInk adds [reading progress and optional statistics][ink-minimal]
to sleep covers. These compositions can inform Note4's final static surface.
A timestamp on that surface describes the saved snapshot. It cannot imply a
clock that continues to update after shutdown.

CrossMux's [StandbyActivity][mux-standby] is a different operating mode. Its
immersive clock can enter light sleep, wake for the face's next tick and redraw.
The requested sleep interval is clamped to 1–60 seconds. Wi-Fi, time sync and
hardware conditions affect whether light sleep is available. This is useful
as a possible future foreground Clock mode. It is not evidence of Note4 deep
sleep current, RTC backup retention or a need to wake the shutdown cover.
Note4 keeps the sequence in [SLEEP_COVER.md](SLEEP_COVER.md): final frame,
peripheral cleanup, rail holds and button wake, with no periodic timer wake.

CrossPoint's [web server][cp-web] copies upload chunks through a bounded
buffer and detects short storage writes. CrossMux's [AirPage workflow][mux-airpage]
adds manual image delivery, foreground live mode and explicit cover selection.
Reconnect alone does not start a download. Its JPEG path uses streamed image
conversion, and selected images replace the saved sleep bitmap only after
successful preparation. Note4's next cover-import flow can reuse these user
expectations through the existing [Send Books session](BOOK_TRANSFER.md):
explicit local access, streamed staging, a preview and a deliberate Apply.
Connectivity owns the radio and HTTP lifetime. Storage owns installation.
Neither the home renderer nor the sleeping device needs a network client.

## Font preparation, antialiasing and dithering

These are three different operations with different costs:

| Operation | Source observation | Note4 decision |
| --- | --- | --- |
| Font caching | CrossInk's [FontCacheManager][ink-font-cache] scans page text/styles and prepares either compressed built-in glyphs or SD fonts. [FontDecompressor][ink-font-decompressor] has four page slots of at most 512 glyphs each, a hot group and allocation statistics. These entry limits are not a total byte budget. | Keep the direct Flash lookup for existing fonts. Consider a bounded cache only when a measured compression or storage cost justifies it. |
| Glyph antialiasing | [GfxRenderer][ink-renderer] reads 2-bit glyph coverage and emits BW, grayscale-MSB and grayscale-LSB passes. It also supports 1-bit glyphs. | Evaluate native 24px bitmaps first. Gray text needs separate panel-quality and refresh-cost evaluation. It is not a free improvement to the current 1bpp page path. |
| Image dithering | CrossInk's [DitherUtils][ink-dither] uses a stateless 4 x 4 Bayer matrix and four output levels. | For future 1bpp covers, evaluate ordered dithering at the final display size. Keep the pattern anchored to display coordinates so repeated dirty updates are stable. |

The local [GlyphBitmap implementation](../components/zectrix_reader/zectrix_reader_font.cc)
indexes eight Unicode ranges into uncompressed Flash records. Each record is
33 bytes: width plus sixteen 16-bit rows. There is no glyph decompression or
SD read to hide with a cache. The [drawing code](../components/zectrix_demo_ui/zectrix_unicode_text.cc)
expands 16px glyphs to 24px with nearest-neighbor sampling. A cache would retain
the same sampled shapes. Native size-specific glyphs address the quality issue
more directly, subject to Flash capacity and font licensing.

The current font already occupies 1,325,973 bytes for 40,181 glyph slots, as
recorded in the [font notice](../components/zectrix_reader/font/README.md).
The E1.1 Full build leaves 149,904 bytes in each existing 3 MiB OTA slot.
Another complete uncompressed glyph set would not fit. Prototype a small
native-size subset or an optional reader font profile before choosing a
compression/storage design. Keep the current partition layout.

For a future compressed or external font, scope preparation to a page, cap
allocated bytes and retain a fallback glyph when preparation fails. Measure
layout time, raster time, storage reads and peak temporary memory separately
from physical refresh time. A faster glyph lookup alone does not establish a
faster page turn. Preserve UTF-8 source anchors through a font change.

Ordered dithering and error diffusion also have different update behavior.
Ordered dithering can reproduce each pixel from a fixed coordinate and value.
Error diffusion carries error between pixels and rows. Recomputing an isolated
dirty rectangle without that history can create seams. Prefer offline or
streamed row conversion for imported artwork, with clipping and output-size
bounds before display.

Flipper's monochrome LCD menu and icon code is a navigation reference. It does
not establish e-paper grayscale behavior. CrossPoint-family grayscale passes
also depend on their display HAL. Note4 keeps SSD2683's existing white full
1bpp preclear before full 4bpp content. No foreign waveform, LUT or animation
cadence is part of this proposal.

## Architecture fit

Flipper's [SceneManager][flipper-scenes] offers enter/event/exit handlers,
per-scene state and Back propagation. `scene_manager_next_scene()` calls exit
and enter synchronously. The [ViewPort implementation in Unleashed][unleashed-viewport]
stores callbacks and enables GUI invalidation through `view_port_update()`.
It uses mutexes and a GUI service. These mechanisms are not the same as
Note4's deferred transitions and clipped canvas scheduler.

CrossPoint's [ActivityManager][cp-manager] stores pending push/pop/replace
operations, a dynamically sized activity stack and a separate render task.
Note4 retains its current ownership model from
[M3](M3_APPLICATION_CONTRACT.md), [SDK v1](SDK_V1.md) and
[M2 display/power rules](M2_PLATFORM_CONTRACT.md):

| Existing Note4 boundary | Consequence for the proposed UI |
| --- | --- |
| One foreground owner calls input, lifecycle and display | Home reads service snapshots. It does not create a rendering task or access hardware. |
| Commands own their payload and resolve after callbacks | Continue Reading passes an owned resume request. A scene cannot destroy itself during an input callback. |
| Sixteen catalog entries, eight scene entries, four viewport slots | Tools is a private Launcher scene. Grouping does not require another global application or an unbounded stack. |
| Shared 400 x 300 1bpp canvas: 15,000 bytes | Draw home cards and list indicators on the existing canvas. Keep the 24px status viewport and existing title/footer areas. |
| Reader: 32 KiB inflate dictionary, 1 KiB input, two 640-glyph pages, 64 recent anchors, at most 128 spine entries | A home summary uses committed bookmark metadata. It does not open or paginate a book during drawing. |
| Kconfig and service-based conditional composition | Derive actions from available descriptors/services. Minimal must not pull reader fonts or networking back into the build. |
| DisplayService owns dirty regions, partial budgets and failure recovery | Compose focus, scroll position and pending status together. The indicator does not issue an extra refresh. |

For responsiveness work, measure input-to-action delay separately from panel
BUSY time. Coalesce obsolete render requests while preserving confirmation,
Back and shutdown events. Any asynchronous display experiment needs one
service owner, bounded pending work and an explicit completion result. Commit
bookmarks and consume refresh budgets only after successful display. Copying
Flipper's viewport locks into application callbacks would not remove a slow
physical refresh and could add lock-order problems.

## Delivered in E1.1

Full currently exposes eleven menu entries through an eight-row window. The
last three destinations previously had no overflow cue. `ShowMenu` now draws
a narrow track in the unused right margin, with a proportional thumb showing
the visible fraction and scroll position. The first window places the thumb
at the top and the last window places it at the bottom. Menus of eight or
fewer entries, including Minimal, omit the indicator.

The drawing uses canvas primitives and local arithmetic. It adds no persistent
state, heap buffer, timer, application dependency or display submission. The
existing selection/wrap behavior remains in the Launcher controller. See
[UI_FLOW.md](UI_FLOW.md) for the current interaction specification.

## Proposed next iterations

1. **Daily Home and Continue Reading.** Present Continue Reading, Library,
   Send Books, Clock, Sleep Cover and Tools in one vertical selection order.
   Continue Reading can use a taller text row while keeping all actions inside
   the existing content area. Use the latest committed local bookmark and
   validate file identity/length when opening. Missing content leads to Library
   with a visible explanation. No bookmark leads directly to Library. Keep
   phone positions subject to the existing explicit resume action. Full shows
   available reading/network actions. Minimal reduces to Clock, Sleep Cover
   and Tools. Tools groups the remaining catalog destinations and retains
   selection on return. Start with the existing font and text fallback.
   An overview card and a few application tiles can share this logical order.
   Test that layout with UP/DOWN as one predictable focus sequence before
   expanding the number of tiles.
2. **Native reading typography.** Compare 16px and native 24px CJK/Latin pages,
   punctuation, line spacing and missing glyphs at actual display resolution.
   Measure font bytes and layout/raster costs in the reader profile before
   adding another full glyph set. Use the current 1bpp path first. Keep optional
   font assets excluded from Minimal. Consider compressed/external font
   preparation only after those measurements show a benefit.
3. **Static personal covers.** Add a bounded Storage-owned cover slot and
   explicit import/preview/apply flow through the local transfer session.
   Start with final-size 400 x 300 1bpp content, which fits a 15,000-byte frame.
   If browser-side conversion is used, the device must still validate format,
   dimensions and complete payload length. Install only after a complete
   staged write. Failed or cancelled uploads retain the previous cover.
   Preserve dashboard/blank fallbacks, the reader-independent sleep UI and
   current partitions. Decode before shutdown so the final power path stays
   bounded. Periodic clock faces remain separate foreground work.

Use ordinary scenario tests for missing books, trimmed modules, cancelled
loading, failed display commits and interrupted cover installation as each
feature is implemented. Reuse the current Host suite, profile builds and
optional panel measurements. These proposals add no release gate or frozen
UI contract.

## Local verification

Run the existing regression and request visual previews from the Host display
fixture:

```bash
mkdir -p build-ui-e1-1
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-ui-e1-1" bash tools/test-minimal-profile.sh
```

The preview fixture includes `launcher.pbm`, `launcher-last.pbm` and
`launcher-minimal.pbm`. They show the actual drawing path at 400 x 300.
They are inspection artifacts, not stored reference images.

E1.1 verification passed all 32 Host targets, including Full/Minimal platform
composition, scenes, status preservation, display failure recovery, streamed
reading and transfer. The focused display suite also passed after adding the
Minimal preview. Visual inspection checked the Full first/last windows and
the eight-entry Minimal menu for content, status and footer overlap.

Both ESP-IDF 5.5.2 firmware builds and the existing artifact comparison passed:

| Measurement | Full | Minimal | Reduction |
| --- | ---: | ---: | ---: |
| Application binary | 2,995,824 bytes | 548,960 bytes | 81.7% |
| Static internal RAM | 213,495 bytes | 118,651 bytes | 44.4% |

The runner writes build logs and `report.json` under `build-profile-regression/`.
Minimal configuration emitted warnings for defaults belonging to excluded
IDF components. Compilation and the component/source-exclusion checks passed.
These figures measure built artifacts, not peak runtime heap or battery life.
Device smoke was not run for this UI-only change. Physical contrast, ghosting,
button latency and sleep current remain hardware measurements.

[cp-home]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/home/HomeActivity.cpp
[cp-reader]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp
[cp-sleep]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp
[cp-web]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
[cp-manager]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp
[biscuit-manager]: https://github.com/yattsu/biscuit/blob/master/src/activities/ActivityManager.cpp
[biscuit-apps]: https://github.com/yattsu/biscuit/blob/master/src/activities/apps/AppsMenuActivity.cpp
[mux-home]: https://github.com/0x1abin/crossmux/blob/main/src/activities/home/HomeActivity.cpp
[mux-apps]: https://github.com/0x1abin/crossmux/blob/main/src/activities/apps/AppsMenuActivity.cpp
[mux-standby]: https://github.com/0x1abin/crossmux/blob/main/src/activities/apps/standby/StandbyActivity.cpp
[mux-airpage]: https://github.com/0x1abin/crossmux/blob/main/src/activities/apps/README.md
[ink-minimal]: https://github.com/uxjulia/CrossInk/blob/main/src/components/themes/minimal/MinimalTheme.cpp
[ink-dashboard]: https://github.com/uxjulia/CrossInk/blob/main/src/components/themes/dashboard/DashboardTheme.cpp
[ink-font-cache]: https://github.com/uxjulia/CrossInk/blob/main/lib/GfxRenderer/FontCacheManager.cpp
[ink-font-decompressor]: https://github.com/uxjulia/CrossInk/blob/main/lib/EpdFont/FontDecompressor.h
[ink-renderer]: https://github.com/uxjulia/CrossInk/blob/main/lib/GfxRenderer/GfxRenderer.cpp
[ink-dither]: https://github.com/uxjulia/CrossInk/blob/main/lib/Epub/Epub/converters/DitherUtils.h
[flipper-scenes]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c
[momentum-menu]: https://github.com/Next-Flip/Momentum-Firmware/blob/dev/applications/services/gui/modules/menu.c
[momentum-keys]: https://github.com/Next-Flip/Momentum-Firmware/blob/dev/applications/services/desktop/desktop_keybinds.c
[momentum-assets]: https://github.com/Next-Flip/Momentum-Firmware/blob/dev/documentation/file_formats/AssetPacks.md
[unleashed-desktop]: https://github.com/DarkFlippers/unleashed-firmware/blob/dev/applications/services/desktop/desktop.c
[unleashed-favorites]: https://github.com/DarkFlippers/unleashed-firmware/blob/dev/applications/settings/desktop_settings/scenes/desktop_settings_scene_favorite.c
[unleashed-viewport]: https://github.com/DarkFlippers/unleashed-firmware/blob/dev/applications/services/gui/view_port.c
