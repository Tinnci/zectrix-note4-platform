# Native pocket tools

E1.9 adds **Home > Pocket Tools** (随身工具), with a focus timer, Gregorian
calendar and tally counter. The tools work offline and need no installed
micro-app. English and Simplified Chinese follow the system language.

## Controls

Hold OK returns one level; hold DOWN uses the normal shutdown and sleep cover.
Hold UP has no action. The tool list remembers its selected row.

| Tool / state | UP / DOWN click | OK click |
| --- | --- | --- |
| Focus: ready | Add / subtract five minutes, bounded to 5–120 | Start focus |
| Focus: running | No action | Pause |
| Focus: paused | UP resets to focus setup; DOWN has no action | Resume the exact remaining duration |
| Focus: complete | UP resets to focus setup; DOWN has no action | Start a five-minute break; after the break, start the next focus interval |
| Calendar | Previous / next month, bounded to 1900–2199 | Open Today / Jump to Month |
| Jump: year | Add / subtract a year within the supported range | Select the month field |
| Jump: month | Add / subtract a month, wrapping within the draft year | Apply the draft and return to the calendar |
| Counter | Add / subtract one, bounded to 0–9999 | Clear; immediately pressing OK again restores the cleared value |

Leaving Jump with Back cancels its draft. Browsing never calibrates the system
clock. A valid TimeService date marks today independently of the RTC weekday
field. Without a valid clock, browsing starts at January 2000 and explicitly
shows **CLOCK UNSET / BROWSE ONLY**; the options initially select Jump. Set
Clock in the normal Clock application to enable Today. The calendar includes
Gregorian leap years, including the different rules for 1900, 2000 and 2100;
it does not supply lunar dates, holidays or solar terms.

## Timer and session lifetime

The default focus duration is 25 minutes. Remaining minutes round up, so zero
means the interval has actually ended. Pause retains microsecond resolution.
Wall-clock, timezone and Companion time corrections do not affect elapsed
time. A delayed callback completes only the current interval; it never catches
up by silently starting additional focus/break cycles. An OK received at the
deadline completes the running interval and needs another OK for the next one.

The shell retains the timer, count, reset undo value, selected month and menu
row in a small RAM session across application destruction/recreation. Users
can return to Launcher or read a book and reopen Pocket Tools to see the
correct remaining time or completion. This is a silent timer: other apps are
not interrupted, and there is no sound, background alarm or timer wake. Power
off or reboot clears the session. The timer and counter screens state this.
Changing the count after a clear replaces the previous undo value.

## Ownership and display work

One optional native application owns six private scenes: Menu, Focus,
Calendar, CalendarOptions, CalendarJump and Counter. CalendarOptions replaces
itself with CalendarJump, so cancelling the draft returns directly to the
calendar. SceneManager defers transitions until event dispatch returns; root
Back uses the shell's saved Launcher parent. The factory only constructs the
controller and borrows the shell-owned session.

The adapter reads the existing Platform-owned TimeService. It needs no new
ServiceRegistry provider, task, GPIO access, RTC alarm, radio session or storage
write. Duration arithmetic uses nonnegative monotonic samples without adding
an overflowing absolute deadline. Calendar work is bounded to twelve months
and drawing to at most 31 date cells.

The UI uses the existing shared 15,000-byte canvas and clipped content viewport.
Initial entry, scene changes and failed-frame retries request Quality;
ordinary visible changes request Fast. Running timer content changes once per
elapsed minute and on completion. Hidden timers do not invalidate the calendar
or counter. Calendar content changes on navigation or a change of current
date/validity. Existing system status updates remain independent. The display
service still decides the actual waveform, dirty bounds and cleanup refresh.
Shutdown retains the selected static cover and adds no wake schedule.

This follows the bounded-work approach of CrossPoint's [reader][reader] and
[activity dispatch][activities], its static [sleep cover][sleep], and explicit
[transfer ownership][transfer]. It reuses Note4's existing implementation of
Flipper-style [SceneManager][scenes] and [ViewPort][views] responsibilities;
no upstream tasks or code are imported for these tools.

## Build and verification

`CONFIG_ZECTRIX_ENABLE_UTILITIES` defaults to `y`, is enabled by Full and is
disabled by Minimal. It has no dependency on Reader, Runtime, Connectivity or
book storage. Disabling it removes the application source, controller,
renderer, Launcher destination and RAM session. The common language catalog
still contains its labels, as it does for other optional applications.

```bash
bash tools/test-utilities.sh
ZECTRIX_UTILITIES_SANITIZE=1 bash tools/test-utilities.sh
bash tools/test-host.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

All 37 Host targets passed, along with utility ASan/UBSan and ShellCheck for
the changed scripts. Utility tests cover exact pause/resume, late deadlines,
clock jumps, minute-only invalidation, foreground recreation, century/month
boundaries, every supported month against the Host UTC calendar, counter
bounds/undo, all six shutdown paths and cancelled drafts. Production renderer
tests run in both languages, including a six-row month, unset clock, retry
after display failure, retained status pixels and no new display heap
allocation. English/Chinese previews were visually inspected. A simulated
25-to-24-minute update transfers 880 bytes through the production SSD2683
driver; this is RAM traffic, not measured panel latency or device power.

| Measurement | Full | Minimal |
| --- | ---: | ---: |
| Application firmware | 3,120,016 bytes | 564,896 bytes |
| Static internal RAM | 213,623 bytes | 118,731 bytes |

Both ESP32-S3 builds and the existing profile comparison passed. Full adds
7,376 bytes compared with E2.2 and retains 25,712 bytes in the existing 3 MiB
application slot. Minimal excludes all utility sources and session RAM; its
shared language catalog grows the image by 1,024 bytes. The Host session is
48 bytes. Partition layout and boot protection are unchanged. No hardware
flash was required; physical button feel, panel timing and standby current
remain hardware measurements.

[reader]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp
[activities]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp
[sleep]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp
[transfer]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
[scenes]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c
[views]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c
