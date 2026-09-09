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
2. **SEND BOOKS** — local Wi-Fi upload, download and book management.
3. **CLOCK** — retained calendar time, offline date/UTC-offset setup and explicit unset/uptime fallback.
4. **SLEEP COVER** — choose a daily dashboard, landscape or blank privacy screen.
5. **SETTINGS** — persist the automatic showcase preference.
6. **CONNECTIVITY** — phone pairing, resource fetch and trusted-phone management.
7. **AUTO SHOWCASE** — unattended rotation of all three display modes.
8. **DISPLAY GALLERY** — individual display previews and measurements.
9. **HARDWARE TESTS** — run all tests or choose one test.
10. **DEVICE INFO** — board information and live power measurements.
11. **ABOUT & LICENSE** — project ownership and license.

Kconfig removes BOOK READER, SEND BOOKS and CONNECTIVITY when their modules
are disabled. Available services determine which selected factories are bound;
menu labels, scrolling and navigation come from that same application catalog.
See [MODULAR_BUILD.md](MODULAR_BUILD.md).

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

The Launcher scrolls its eight visible rows to reach all eleven items. Reader
uses Library -> Reading -> Options. UP/DOWN turn pages in Reading; OK opens
font/resume/restart/save options. Long OK returns one scene, and loading remains
cancellable. Font changes preserve the current source anchor. A successful
display saves progress to NVS and the C1 outbox. Phone progress is offered for
explicit application, never used to move an active page automatically.
See [READER.md](READER.md) for supported books and content installation.

Send Books uses Mode -> Session. UP/DOWN selects a temporary hotspot or a saved
home network. OK starts the session, which shows SSID, access code, HTTP address
and upload progress. OK during transfer finishes it. Completion OK opens the
reader, or returns home when the reader is disabled. Hold OK stops and returns
to Mode, then home. HTTP and radio work run
under Connectivity ownership. Progress renders are limited to one per second
and 10-percent steps or saved-book count changes. See
[BOOK_TRANSFER.md](BOOK_TRANSFER.md) for browser controls and timeouts.

Clock uses View -> Edit. OK opens date/time setup, UP/DOWN changes a field,
and OK advances to a final Save. Hold OK cancels the draft and returns to View,
then home. Save sets system time and attempts RTC persistence; failures show
`SAVE PENDING` while the platform retries. An authorized Companion reconnect
also calibrates automatically. See [TIME.md](TIME.md).

Sleep Cover uses Choose -> Preview. UP/DOWN selects the style; OK saves and
previews it, then OK in Preview sleeps. Hold OK returns one scene. The active
style also applies to global hold-DOWN shutdown. The dashboard shows the last
committed reader position and calendar; landscape shows a daily line; blank
clears the full panel. See [SLEEP_COVER.md](SLEEP_COVER.md) for persistence and
sleep/wake behavior.

## Persistent status bar

Every active page reserves the top 24 pixels for status. The title occupies
the next 20 pixels; content and footer stay below the status viewport.

| Indicator | Source and behavior |
| --- | --- |
| `HH:MM` | The shared TimeService snapshot, restored from RTC or calibrated by the user/phone; `--:--` if unset. Seconds do not cause refreshes. |
| Battery and percentage | Existing calibrated, averaged ADC power snapshot, sampled every five seconds. An unavailable/absent battery shows `--%`. |
| Lightning / `+` / `!` | Charging, external power without charging, or charger fault respectively. |
| Bluetooth symbol | `OFF`, `ON` (idle/advertising), `...` (pairing/securing), `LINK` (transport connected), or `ERR`. LINK does not assert peer authorization or sync convergence. |
| Wi-Fi symbol | `OFF` until station/hotspot startup, `...` while connecting/stopping, `LINK` after IP acquisition or while the book server is available, or `ERR` if radio shutdown fails. Stored credentials alone do not indicate an active radio. |

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
WAIT/RUN/PASS/FAIL/SKIP state. With Wi-Fi disabled, RF reports SKIP and the
summary excludes it from executed tests and failures. The selected test uses
an inverted cell. Instructions
and measurements use the full content width below the strip. Shorten
exceptionally long runtime values with an ellipsis.
Interactive tests use explicit operator prompts. Long OK cancels the active
test. Long DOWN retains its global shutdown meaning.

## Shutdown sequence

1. Exit the foreground application, then stop maintenance and connectivity.
2. Capture time, power and saved reading. Present the selected cover with a
   full 1bpp refresh and wait for completion; failure attempts one white clear.
3. Stop status composition on the final surface and release DisplayService
   and the other service consumers.
4. Release board peripherals, then turn off the indicator LED and audio rail.
5. Wait for DOWN release, arm GPIO18 button wake, then release the battery latch.
6. Enter deep sleep if USB keeps the board powered. Release and press DOWN to
   wake into a fresh boot.

The final cover replaces live status icons with a static battery snapshot.
Its **AS OF** timestamp does not advance during sleep. Blank clears all pixels.
Shutdown proceeds even if display or wake setup fails. If DOWN stays held
beyond the roughly five-second release wait, USB sleep requires reset or power
cycling. No timer wake or periodic display update is scheduled.

## Reference designs and continuation

L1 uses independently implemented adaptations of these upstream designs:

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
  L1.3 applies the web-transfer lifecycle through Connectivity and Storage.
  L1.4 composes its static dashboard/landscape/blank surface before the existing
  power transition, using the same private scene and viewport ownership.

Local verification includes scene-stack bounds and callback order, gallery
back/rotation/error paths, invalid RTC/system/uptime behavior, clipped canvas
composition, status-only dirty regions, gray-content preservation, display
failure recovery, final cover retention, privacy clearing and button-wake
preparation. For optional visual
inspection, set `ZECTRIX_UI_PREVIEW_DIR` to an existing directory when running
`tools/test-display-service.sh`; it writes PBM previews without a golden-image
comparison or release gate.
