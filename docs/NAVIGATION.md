# Unified navigation

E1.4 gives the first-party terminal one button vocabulary and predictable
parents. Applications share one foreground runtime, the existing bounded
SceneManager and the existing status/content viewports.

## Controls

| Gesture | Behavior |
| --- | --- |
| UP / DOWN click | Move focus backward / forward, wrapping in lists and tiles. Reader turns pages; Clock edits the selected value. |
| OK click | Open the focused destination or perform the displayed action. |
| Hold OK | Return one level. At an application's root, return to its Launcher parent. Home itself stays on Home. |
| Hold DOWN | Shut down with the selected static sleep cover from any first-party page, including loading and preview pages. |
| Hold UP | No action. |

`MapNavigation` maps SDK input to these intents independently of GPIO sampling.
Pocket Tools keeps its timer/calendar/counter inside private scenes. Focus and
counter use the actions printed in their footers; Calendar offers month paging,
Today and a cancellable year/month draft. All retain the same Back/shutdown
gestures. See [Pocket tools](UTILITIES.md) for the controls and RAM lifetime.

Phone removal is an explicit menu action: Forget opens a child scene with
**KEEP TRUSTED PHONE** selected. Confirming **FORGET PHONE AND SYNC LINK** uses
the existing Connectivity and Reader reset operations. Back cancels; holding
UP cannot erase trust. Pairing and resource fetch also use focused rows.

Hardware diagnostics retain the click prompts needed for an active test.
Hold OK cancels it, and hold DOWN requests shutdown at the existing cancellation
polls. Auto Showcase retains its click-to-exit behavior because it has no
selection. Sleep uses the existing three-second DOWN gesture; release DOWN
after requesting sleep, then press it again to wake.

## Launcher parents

```text
Home -> application root -> private child scenes
Home -> Tools -> tool root -> private child scenes
```

Back from Device Info, Connectivity, Gallery or Diagnostics returns directly
to Tools with the previous row selected. Another Back returns to Home with
the Tools tile selected. Ordinary Home tiles return to their original Home
focus. Both focus values and the Launcher scene are kept in owner RAM.

The shell submits the deferred SDK `Back` command and sets a one-shot parent
return flag only after acceptance. The Launcher candidate copies that flag
before the outgoing application's exit, and its factory consumes the flag even
if allocation fails. Launcher restores Home and, when needed, pushes Tools
through its normal scene handler. It does not retain suspended application
objects. The catalog, SDK commands and persistent data formats are unchanged.

An explicit SDK `Home` or an entry-failure fallback starts on Home, preserving
the saved focus. A failed candidate allocation leaves the current application
active; Back can be retried. A missing Tools group falls back to Home. The SDK
runtime still resolves both Home and Back to Launcher; parent restoration is
first-party shell behavior, not a new global application stack.

The finished Send Books action replaces the transfer application with Reader's
Library. Exiting transfer stops its service before Reader entry. Leaving that
Library returns to the originating Home focus; Back never reopens the expired
HTTP session. Builds without Reader return Home from the finished transfer.

## Private scene behavior

| Application | Navigation and cleanup |
| --- | --- |
| Reader | Library -> Reading -> Options. Back from Options keeps the book open; Back from Reading cancels work and closes it before Library refresh. Only successfully displayed pages advance saved progress. |
| Send Books | Mode -> Session. Back stops transfer and returns to Mode with the selected mode retained. Exit also requests service stop; Connectivity retains ownership while a failed stop retries. |
| USB Manager | One dashboard. OK cancels the host session or retries unavailable storage. Back returns to Tools. Exit closes managed files, aborts staging and releases the library lease before another application enters. |
| Sleep Cover | Choose -> Preview. Back returns to Choose; its already saved preference remains applied. Preview OK or global shutdown uses the normal final-cover sequence. |
| Clock | View -> Edit. Back discards the draft. Save returns to View. Scene changes and failed-frame retries request Quality. |
| Connectivity | Actions -> Forget. Back cancels removal and restores the action row. Exit stops private scenes and clears the displayed passkey; Platform continues to own BLE and resource work. |
| Diagnostics | Mode -> Individual -> Running for one test; Mode -> Running for all. Completion of one test returns to its selected row. Successful Run All replaces Running with Summary; OK or Back returns to Mode. Cancellation during a test or its result wait returns to the parent without starting another test. |
| Gallery | Menu -> Preview, replacing Preview with Report. Back returns to the selected menu row. Exit stops timed frame advancement. |
| Settings | Options -> Language. Back cancels an unapplied language selection and retains the parent row. OK applies/saves a language or toggles/saves automatic showcase; failed saves can be retried. |
| Device Info / About | Single root page. Back returns to the Launcher parent. |

Scene transitions execute after event callbacks return. Enter/exit callbacks
cannot navigate recursively, and an unconsumed Back pops exactly one child.
Only an unconsumed root Back becomes a shell command. Exits remain idempotent,
including partial entry. Stop clears pending work; later input/ticks cannot
restart it. Retained selections are copied before stopping the controller.

Diagnostics unwinds Running even if an intermediate display call fails, then
requests its parent frame through the normal Render callback. Final summaries
also use Render. Hardware progress callbacks retain their existing synchronous
display access on the foreground owner. No worker or additional input loop is
introduced. Back dismisses the splash; shutdown is honored during splash and
result waits.

Shutdown has priority over navigation. Runtime exit saves/closes Reader or
stops transfer before the shell captures the sleep cover, stops services and
releases peripherals. No later foreground frame is rendered after shutdown.
See [SLEEP_COVER.md](SLEEP_COVER.md) and [M3_APPLICATION_CONTRACT.md](M3_APPLICATION_CONTRACT.md).

## Maintenance observation

D1.4's `app list`, `app current` and `scene dump` copy the foreground catalog,
generation, private scene stack, viewport regions and Lua memory quotas at an
owner safe point. SceneManager publishes to shell-owned storage; destruction
clears that copy. USB formatting never holds a manager, application or VM
pointer. `input watch` observes the board's separate trace ring and cannot
consume the events used here. Confirmed maintenance shutdown/reboot/reset
waits until SDK callbacks unwind and runs normal foreground Exit first.
See [MAINTENANCE_CLI_CONTRACT.md](MAINTENANCE_CLI_CONTRACT.md) for commands,
confirmation scope and the distinct accepted/unknown outcomes.

## References and verification

This follows CrossPoint's [deferred activity transitions][activities],
[cooperative EPUB reading][reader], [static sleep composition][sleep] and
[streamed web transfer cleanup][transfer], and Flipper Zero's
[scene-local state and Back propagation][scenes] and [viewport ownership][views].
Upstream source was reviewed for this iteration. Note4 retains its own bounded
deferred scene stack and single display owner; no upstream code or assets are
imported. See [FIRMWARE_UI_STUDY.md](FIRMWARE_UI_STUDY.md).

```bash
mkdir -p build-ui-e1-4
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-ui-e1-4" bash tools/test-host.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

Host coverage includes Tools restoration and fallback, selection retention,
forget cancellation/confirmation, diagnostic completion/cancellation/summary
returns, reader cancellation and committed progress, transfer stop retries,
stopped-controller input/ticks, deferred scene exits and display composition.
The preview fixture exports connection actions, passkey and Forget screens.

Validation passed all 32 Host targets, focused navigation ASan/UBSan and
Full/Minimal ESP32-S3 builds. Connection previews were visually inspected.
Device smoke was not run for this application change; physical button flows
remain hardware qualification work.

[activities]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp
[reader]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp
[sleep]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp
[transfer]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
[scenes]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c
[views]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c
