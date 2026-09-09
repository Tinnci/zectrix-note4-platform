# UI interaction specification

## Design goals

- Provide a practical pocket terminal with a stable home menu and live system status.
- Keep every screen readable in black and white without antialiasing.
- Reserve partial refresh for small, repeated state changes.
- Keep shutdown accessible without adding a dedicated settings screen.

## Navigation model

UP and DOWN change selection as soon as the debounced press is detected. OK
confirms on release, a 1.5-second OK hold returns or cancels, and a 3-second
DOWN hold requests global shutdown. Selection wraps at the top and bottom of
each menu.

The home menu contains:

1. **BOOK READER** — TXT/EPUB library, paginated reading, font size and saved progress.
2. **CLOCK** — calendar time, with system time or explicit uptime fallback when RTC fails.
3. **SETTINGS** — persist the automatic showcase preference.
4. **CONNECTIVITY** — phone pairing, resource fetch and trusted-phone management.
5. **AUTO SHOWCASE** — unattended rotation of all three display modes.
6. **DISPLAY GALLERY** — individual display previews and measurements.
7. **HARDWARE TESTS** — run all tests or choose one test.
8. **DEVICE INFO** — board information and live power measurements.
9. **ABOUT & LICENSE** — project ownership and license.

Returning home restores the previous selection. Auto Showcase is off by
default for new installations; valid existing preferences are retained. If
enabled, it starts after 15 seconds without physical input on the home screen.
A click or long OK leaves it after the active physical refresh completes.

All top-level destinations use the application runtime. Gallery uses a private
bounded scene stack: OK opens a preview, OK during a preview opens its report,
and OK on the report returns to the gallery menu. Long OK pops to the previous
menu; at a root screen it returns home. Menu selection survives push/pop.
Run All returns to its menu after three previews. The rotation and footprint
animation use idle deadlines rather than separate blocking input loops.

The Launcher scrolls its eight visible rows to reach the ninth item. Reader
uses Library -> Reading -> Options. UP/DOWN turn pages in Reading; OK opens
font/resume/restart/save options. Long OK returns one scene, and loading remains
cancellable. Font changes preserve the current source anchor. A successful
display saves progress to NVS and the C1 outbox. Phone progress is offered for
explicit application, never used to move an active page automatically.
See [READER.md](READER.md) for supported books and content installation.

## Persistent status bar

Every active page reserves the top 24 pixels for status. The title occupies
the next 20 pixels; content and footer stay below the status viewport.

| Indicator | Source and behavior |
| --- | --- |
| `HH:MM` | RTC, or valid system time if RTC fails; `--:--` if neither is set. Seconds do not cause refreshes. |
| Battery and percentage | Existing calibrated, averaged ADC power snapshot, sampled every five seconds. An unavailable/absent battery shows `--%`. |
| Lightning / `+` / `!` | Charging, external power without charging, or charger fault respectively. |
| Bluetooth symbol | `OFF`, `ON` (idle/advertising), `...` (pairing/securing), `LINK` (transport connected), or `ERR`. LINK does not assert peer authorization or sync convergence. |
| Wi-Fi symbol | `OFF` until station startup, `...` while connecting/stopping, `LINK` after IP acquisition, or `ERR` if radio shutdown fails. Stored credentials alone do not indicate an active radio. |

One owner loop samples state between application callbacks with a 250 ms input
wait. Only visible changes invalidate the status viewport; an application
redraw includes pending status in the same commit. Status-only 1bpp updates
preserve the application canvas and use DisplayService's minimal dirty region.
Clock remains navigable during RTC failure, labels its fallback source and
automatically resumes RTC display after recovery.

## Refresh policy

- Splash, scene changes, initial reader pages, font changes, reports and summaries use full 1bpp refresh.
- Reader page turns request Fast; DisplayService selects partial or full refresh.
- Menu selection and live test content use partial 1bpp refresh.
- After eight UI partial refreshes, promote the next update to full refresh.
- Test updates are throttled to 500 ms unless a PASS/FAIL state must be shown.
- Precede full 4bpp content with a white full 1bpp refresh. While a gray preview
  is visible, retain its gray buffer for status updates; the panel requires a
  full gray refresh for these updates too.

## Display gallery

Each individual scene displays content first. It then displays an information page with
the refresh mode, pixel format, frame-buffer size, measured duration and
ESP-IDF result name.

| Scene | Initial operation | Animated operation |
| --- | --- | --- |
| Lighthouse | Full 1bpp image | None |
| Footprints | Full 1bpp snowy path | Six cumulative partial 1bpp steps |
| Mountain | White full 1bpp clear | Full 4bpp, 16-gray image |

Existing 400x300 assets keep their pixel coordinates; rows 0 through 23 are
reserved for status. Footprint patches are cumulative and clipped below this
boundary. Display measurements sum physical rendering time, excluding preview
hold times. Malformed image/patch input or a display failure leads to a report
while return and shutdown remain available.

## Hardware-test screen

A full-width strip below the page title shows all seven tests and their
WAIT/RUN/PASS/FAIL state. The selected test uses an inverted cell. Instructions
and measurements use the full content width below the strip. Shorten
exceptionally long runtime values with an ellipsis.
Interactive tests use explicit operator prompts. Long OK cancels the active
test. Long DOWN retains its global shutdown meaning.

## Shutdown sequence

1. Do a white full 1bpp refresh and wait for completion.
2. Power off the display through the driver.
3. Turn off the indicator LED and audio rail.
4. Release the battery power latch.
5. Enter deep sleep as a USB-powered fallback.

## Reference designs and continuation

L1.1 uses independently implemented adaptations of these upstream designs:

- Flipper Zero [SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
  provides the enter/event/exit handler pattern, retained scene state and Back
  propagation. Zectrix bounds the stack and defers transitions until event
  return on its existing owner task.
- Flipper Zero [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
  separates bounded drawing from invalidation. Zectrix uses fixed slots, canvas
  clipping and one DisplayService commit without another GUI task.
- CrossPoint [ActivityManager](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp)
  separates pending navigation, activity lifetime and requested rendering.
  Zectrix keeps its SDK lifecycle and a single owner instead of adopting the
  upstream render task and dynamically sized activity stack.
- CrossPoint [EpubReaderActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp)
  loads the current section page, keeps pagination position and bounds page-load
  retries. The reserved content viewport and retained menu/scene state support
  this page-at-a-time approach in L1.2; L1.1 does not yet add text/EPUB decoding.
- CrossPoint [SleepActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp)
  handles cover placement and sleep composition, and
  [CrossPointWebServer](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp)
  streams upload chunks through a bounded write buffer and detects short writes.
  These inform the separate L1.4 ambient-cover and L1.3 LAN-ingestion tasks.

Local verification includes scene-stack bounds and callback order, gallery
back/rotation/error paths, invalid RTC/system/uptime behavior, clipped canvas
composition, status-only dirty regions, gray-content preservation, display
failure recovery and the final white shutdown surface. For optional visual
inspection, set `ZECTRIX_UI_PREVIEW_DIR` to an existing directory when running
`tools/test-display-service.sh`; it writes PBM previews without a golden-image
comparison or release gate.
