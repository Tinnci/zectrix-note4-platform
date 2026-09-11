# Reliability and foreground recovery

D1.5 protects a usable local terminal when an app, a transfer or persisted
settings fails. It builds on CrossPoint's cooperative reading/streamed uploads
and Flipper's scene/view ownership, as reviewed in
[FIRMWARE_UI_STUDY.md](FIRMWARE_UI_STUDY.md). Recovery runs after the current
callback returns; no second rendering task, foreign waveform or upstream code
is introduced.

## Runtime behavior

- Three consecutive returned foreground errors in one application generation
  trigger controlled recovery. Successful work or navigation clears the streak.
  Failed renders remain pending so transient I/O can retry. The owner frees a
  faulty app before allocating Home. Normal navigation still creates its
  candidate first, preserving the current app on allocation failure.
- A failing Home uses the shared monochrome canvas for a bilingual recovery
  page. It releases retained grayscale content and allocates no application or
  extra framebuffer. OK/Back retries Home, holding DOWN powers off, and USB
  inspection/confirmed maintenance remain available. Failed page refreshes stay
  pending while input remains usable.
- NVS initialization/open errors preserve settings, bookmarks and bonds.
  Basic UI/time/file access continues with persistence errors visible. The
  independent book/app partition never formats on normal mount failure. Factory
  reset remains the explicit erase path, including failed NVS initialization.
- Panic/watchdog resets start in Home with radios and automatic showcase
  disabled for that boot. NVS failure also keeps radios stopped. A normal reboot
  restores radios when settings are healthy. Any SDK error or owner recovery
  suppresses automatic showcase until reboot without rewriting the preference.
- Upload handles latch short writes and flush/sync/close/rename failures.
  Abort/reopen is required before accepting more chunks or committing. Interrupted
  staging is removed at the next management session; published books are kept.
- Wi-Fi teardown retries transient failures only within its existing two-second
  deadline. Radio ownership is released only after successful cleanup; a
  persistent failure remains observable and blocks reuse.

## Watchdog ownership

BootGuard arms the inherited RTC watchdog for 60 seconds. Known-good/factory
images release it and Platform immediately arms the 90-second runtime watchdog
before board initialization. A trial image keeps its original confirmation
deadline until the first successful Home frame and image confirmation;
`Platform::ConfirmBoot()` then starts runtime protection. A recovery page cannot
confirm a trial. Failed startup or failed image confirmation retains protection.

Only completed foreground slices and completed built-in diagnostic items feed
runtime protection. Each interactive diagnostic item already has a 60-second
deadline. USB/time polling, inner input waits, timers and radio tasks do not
feed it, so a blocked native callback or teardown cannot conceal its own hang.
The supervisor refuses to feed after a missed deadline. The RTC hardware resets
independently of foreground scheduling; existing task/interrupt watchdogs and
Lua's 10,000-instruction/128 KiB limits remain in place.

The watchdog stays armed through application, service and board teardown. The
power owner invokes a final hook after peripheral release and wake setup, just
before battery cut/deep sleep. Reboot disarms after service cleanup. Ordinary
destruction never disarms the runtime or trial watchdog.

`system health` copies a fixed-size owner snapshot without allocating or feeding
the watchdog. It reports progress times, error/recovery-request counters,
watchdog state, NVS error and boot policy. `system info`, `system heap` and
`system tasks` provide reset reason, free/minimum/largest heap and stack
watermarks. All health counters are volatile and saturating; no flash crash
counter, journal or additional checksum is maintained.

## Fault and soak tests

| Executed path | Fault or repetition | Observable result |
| --- | --- | --- |
| StorageService NVS adapter | Init/open/read/commit errors, explicit reset after failed init | No automatic erase; native errors remain visible |
| Real POSIX upload I/O | Child exits midway; file-size limits force short writes and flush failure | Published file preserved; staging reclaimed; poisoned handle refuses retry |
| ESP HTTP adapter with loopback TCP | 120 cancellation/restart cycles with blocked headers and uploads | Workers finish before Storage release; staging is removed and management can reopen |
| ApplicationRuntime | 4,096 fault/recovery cycles with failed factories, render/event/idle/Exit errors and failed Home allocation | Exactly-once cleanup, reentry rejection, explicit failsafe retry; zero live app objects after Stop |
| ESP Wi-Fi driver with IDF fakes | 1,024 consecutive timeouts with stop/deinit/unregister faults | HTTP/TLS/netif/handler/radio ownership released before reconnect |
| Lua engine and real Flashcards pilot | 1,024 pilot/fault/restart cycles | Instruction/memory errors remain bounded; live guest heap returns to zero |
| Shared UI and SSD2683 driver with fake SPI | 256 recovery/clock/sleep cycles per language, injected display errors | Recovery retries, static sleep surface, no drawing allocations or retained driver handles |
| HealthSupervisor virtual clock | 604,800 one-second completions across a 32-bit millisecond boundary | Seven simulated days without counter/time loss or spurious deadline expiry |
| Boot/platform/power adapters | Trial handoff, failed confirmation, six platform profiles and final power hook | Trial protection retained; polling cannot feed; runtime protection covers cleanup |
| Localization and generated UI subset | Three reader/Chinese configurations; subset pixels compared with the packed reader font | English and Chinese recovery text renders correctly; the generator decodes the existing shared-tile format |

All 40 targets passed in `bash tools/test-host.sh`. Focused entry points are
`test-health-supervisor.sh`, `test-application-runtime.sh`, `test-storage-service.sh`,
`test-wifi-backend.sh`, `test-book-transfer.sh`, `test-runtime.sh`, `test-display-service.sh`,
`test-platform.sh`, `test-update-service.sh` and `test-power-service.sh` under
`tools/`. ASan/UBSan passed for application recovery, storage, health, platform,
display, Lua runtime and all three localization profiles. Optional sanitizer
variants exercise the same production paths; no firmware corruption or
forced-crash command is exposed to users.

## Build and device evidence

Full and Minimal firmware builds and the existing profile comparison passed.
Full is 2,495,312 bytes (+2,736 from E1.10); Minimal is 521,648 bytes (+1,344).
Static internal RAM remains 201,815 / 108,323 bytes, including 72,112 / 31,824
bytes of data/BSS. The supervisor uses the existing Platform allocation and
does not create a monitoring task.

The connected ESP32-S3 passed Full flash/boot smoke, PSRAM/partition checks and
the first Launcher frame/boot confirmation. With one serial connection kept
open, two `system health` queries showed completed progress advancing from
7,820 to 102,970 ms and heartbeats from 11 to 378. Runtime protection remained
armed and unexpired across that 95.15-second interval, with a maximum observed
completion gap of 4,413 ms. This verifies ordinary foreground feeding across
one 90-second deadline. Host tests exercise deadline expiry and refusal to feed
late work; physical forced-hang reset remains unmeasured.

These tests accelerate state transitions and inject errors at storage/IDF
boundaries. A child process exit is not electrical flash power interruption,
and injected NVS errors do not measure physical NVS CRC recovery. Simulated days
are not physical endurance hours. Issue [#56](https://github.com/Tinnci/zectrix-note4-platform/issues/56)
still needs interrupted physical OTA/A-B rollback evidence; Issue
[#57](https://github.com/Tinnci/zectrix-note4-platform/issues/57) still needs the
separate display/reader/transfer/sleep/RTC hardware measurements. Normal build
or boot smoke evidence does not close either physical qualification task.
