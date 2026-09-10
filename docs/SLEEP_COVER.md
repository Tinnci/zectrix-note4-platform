# Ambient sleep cover and dashboard

L1.4 adds **SLEEP COVER** to the Launcher. Choose a daily dashboard, a quiet
landscape or a blank privacy screen. The selected surface is drawn before
power-off and remains visible without keeping the display or radios active.

## Controls

| Scene | UP / DOWN | OK click | Hold OK |
| --- | --- | --- | --- |
| Choose | Select a style | Save the choice and preview it | Return to Launcher parent |
| Preview | No change | Sleep now | Return to Choose |

Hold DOWN for three seconds from any normal application to sleep with the
active style. Release DOWN after requesting sleep; press it again to wake.
The preview keeps the live status bar and button hints. Final sleep replaces
that bar with **NOTE4 / AT REST** and a battery snapshot, or clears every pixel
for the blank style.

E1.6 gives the final dashboard and landscape a black footer band with the
centered white hint **PRESS DOWN TO WAKE**. Preview retains its live OK/Back
controls and save-failure hint. The blank final style stays entirely white.

## Styles and saved settings

| Style | Stored value | Final surface |
| --- | --- | --- |
| DAILY DASHBOARD | `0` (default) | Day, month calendar, capture time, last saved book and reading progress, and a daily line |
| QUIET LANDSCAPE | `1` | Mountain illustration, daily line and capture date/time |
| BLANK / PRIVACY | `2` | Entirely white, including the status and footer areas |

`StorageService` stores the unsigned 32-bit preference at `ui.sleep_cover`.
A missing or invalid value uses the dashboard. A missing key needs no boot-time
write. Choosing a different style saves it immediately; a failed save shows
**NOT SAVED**. The chosen style still applies for the current boot and sleep
remains available. Return to Choose and select it again to retry persistence.

The dashboard reads the latest successfully committed local reader bookmark
after the Reader has exited. It uses the existing bookmark store and CJK font,
with no separate progress record. A failed bookmark save leaves the previous
saved position visible. A missing or unreadable store shows an empty reading
section. The displayed book can be one that was subsequently deleted from the
library; **LAST SAVED READING** describes reading history, not file availability.

Calendar data comes from RTC, then valid system time. Uptime or an invalid
calendar displays **TIME NOT SET**. The month grid handles leap years and six
week rows, and calculates weekdays from the date. Seven original short quotes
rotate by calendar day; repeated previews on the same day use the same quote.
The mountain illustration uses the existing monochrome canvas primitives.

**AS OF** marks when the snapshot was taken. Preview captures on selection;
shutdown captures again after foreground exit. The calendar, quote and battery
value stay static throughout sleep. L1.4 adds manual sleep, with no automatic
idle sleep, periodic refresh or timer wake. It is a retained display surface,
not an access-control lock.

## Scene, display and power ownership

The Sleep Cover application uses the existing bounded `SceneManager` for
Choose -> Preview. Input callbacks request deferred navigation or rendering.
Failed preview commits request a Quality retry through the next idle callback.
The implementation uses the shared 15,000-byte 1bpp canvas and existing glyphs;
it adds no framebuffer allocation, image decoder or render task.

Shutdown runs on the application owner:

1. Exit the foreground application so reading saves/closes and transfer stops.
2. Stop maintenance and connectivity, then read time, battery and saved reading.
3. Present the selected surface through DisplayService's full 1bpp path and
   wait for completion. If it fails, attempt one white full-refresh fallback.
4. Suppress pending status updates for the final surface. Release DisplayService
   and the remaining platform consumers without repainting the panel.
5. Release board peripherals, turn off the LED/audio rail and keep the existing
   100 ms delay. Prepare button wake, release the battery latch, retain the
   existing second 100 ms delay and enter deep sleep if USB still supplies power.

Display or wake setup failure is logged and does not skip cleanup or rail-off.
Existing GPIO holds retain the disabled rails. The application never controls
GPIO, RTC wake registers or deep sleep directly.

Board support waits for three released DOWN samples at 20 ms intervals before
arming GPIO18 as RTC input with a pull-up and EXT1 ANY_LOW wake. Waiting is
bounded to about five seconds to prevent a held shutdown button from causing
an immediate restart or indefinitely delaying rail-off. The RTC peripheral
power domain is not forced on. Boot releases the wake pin's RTC hold and mode
before normal button sampling resumes.

If DOWN stays held past the bound, or wake configuration fails, USB-powered
sleep has no button wake; use reset or power cycling. Battery-powered shutdown
continues to use the board's hardware latch. Neither the retained calendar nor
the quote schedules a wake.

## Verification and references

```bash
bash tools/test-host.sh
ZECTRIX_SLEEP_SANITIZE=1 bash tools/test-sleep-cover.sh
ZECTRIX_READER_SANITIZE=1 bash tools/test-reader.sh
bash tools/test-power-service.sh
mkdir -p /tmp/zectrix-sleep-preview
ZECTRIX_UI_PREVIEW_DIR=/tmp/zectrix-sleep-preview bash tools/test-display-service.sh
```

Host tests cover calendar validation, quote stability, scene navigation and
display retries, committed bookmark selection, preview/status composition,
gray-to-1bpp shutdown, privacy clearing, failed-cover fallback and retention
through display destruction. Production board code is tested with released,
delayed-release and stuck buttons, failed wake setup and fresh-boot RTC pin
restoration; existing resource and rail-hold checks remain active.

The L1.4 iteration passed all 30 Host targets, the focused ASan/UBSan checks,
ShellCheck, the ESP32-S3 firmware build and connected-device flash/boot smoke.
Rendered menu, dashboard, landscape, six-row calendar, unset-clock and blank
previews were inspected. Physical cover selection, sleep/wake behavior and
standby current were not measured by the boot smoke test. Microamp-level
consumption remains a board measurement, not a result inferred from Host tests.
See [TEST_CRITERIA.md](TEST_CRITERIA.md#sleep-cover-and-wake-check) for manual steps.

CrossPoint's [SleepActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp)
informs final cover composition, a blank option and display fallbacks.
Flipper Zero's [SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
inform private scenes, Back propagation and bounded drawing. These are
independent adaptations to the existing Note4 services; no upstream
application code is copied.
