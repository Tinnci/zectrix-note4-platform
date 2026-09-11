# Development roadmap

The project uses stage gates. A later milestone can start only when its stated
dependencies are satisfied. Advanced E-Ink quality research runs in parallel
and does not block unrelated input or power work.

[G1.1 triage](GITHUB_TRIAGE.md) maps the autonomous backlog to GitHub Issues
and Milestones. C1 and D1 use different subtask numbering in the two systems.
Completed software slices do not close an older Issue whose remaining command
or physical acceptance work is still outstanding.

## M1 — Reproducible reference baseline

Goal: reproduce the upstream hardware demo without architectural changes.

- Pin ESP-IDF 5.5.2 and component dependencies.
- Record firmware version, Git commit and partition version at boot.
- Produce firmware hashes and a size report.
- Document build, flash, monitor and factory-recovery procedures.
- Run and record the existing seven hardware self-tests.

Exit: a clean checkout builds with the pinned toolchain and boots on the target
board. A verified factory restore must then succeed.

## M2 — Minimum platform services

Goal: prevent application code from controlling hardware directly.

- Add display, input, power, time, storage and system service boundaries.
- Move EPD baseline and refresh-state ownership into the display service.
- Define a common event type and non-blocking dispatch model.
- Add a Diagnostics application that uses platform services only.

Exit: Diagnostics exercises the primary hardware without raw GPIO, SPI or
deep-sleep calls.

## M3 — Static application platform

Goal: run multiple statically linked applications through one lifecycle.

Status: Complete on qualified commit `4dc371a`.

- Add application descriptors and a static registry.
- Use owned deferred commands and foreground-generation render requests.
- Add Launcher, Settings, Diagnostics and Clock.
- Keep platform implementation types out of public application headers.

Exit: at least three applications use only the draft public API.

## M4 — SDK v1

Goal: freeze a source-stable SDK v1 for statically linked applications.

Status: Complete on qualified firmware commit `91043ea`.

- Keep ESP-IDF and FreeRTOS below the application source contract.
- Add API compatibility tests and semantic versioning.
- Define ownership, lifecycle, execution, error and deprecation policy.
- Migrate all M3 applications to the versioned contract.

Exit: SDK v1 has an explicit source-compatibility guarantee and passes the
unified software and hardware gate. No binary ABI is promised.

## M5 — Update architecture

Goal: select a safe update and partition layout from measured requirements.

Status: Architecture and M5.1/M5.2 implementation complete. M5.1 implements
A/B partition validation, native OTA rollback and a hardware watchdog for
unconfirmed startup. The owner confirms
a trial after platform initialization and the first launcher render. See
[ADR-0005](adr/0005-ab-ota-boot-confirmation.md). M5.2 adds streamed CRC-32 and
header verification, bounded native image validation, flash readback and boot
selection. The measured architecture decision in GitHub #7 is resolved.
Trusted update delivery and hardware fault injection remain explicit
[follow-up work](GITHUB_TRIAGE.md#remaining-work).

- Measure maximum firmware, assets and user-data requirements.
- Select an A/B OTA, rollback and recovery design.
- Record the selection in an architecture decision record.

Exit: the partition layout and update path have explicit compatibility and
recovery guarantees.

## C1 — Connectivity platform

Goal: add a BLE-first companion channel and an on-demand Wi-Fi data path
without exposing radio or RTOS mechanisms to applications.

Status: Implementation delivered; physical qualification remains open. This
milestone is independent of M5 and dynamic application research. The protocol,
durable synchronization, policy, secure BLE/Android path, NFC-assisted
enrollment, controlled phone HTTPS resource path, production ESP-IDF Wi-Fi
driver and direct HTTPS escalation are implemented. Q1 adds callback/cleanup
regressions and E1.5 adds Wi-Fi/BLE arbitration. GitHub #34–#39 and #48 retain
their specified Android/Note4 and RF/power evidence requirements. #29 and #38
remain open until their dependencies and end-to-end evidence are complete.

- Use ESP-NimBLE for a Note4 peripheral and an Android central.
- Carry a versioned companion protocol over two transport characteristics.
- Distinguish durable state, command/reply and stream data.
- Persist unacknowledged state and resume it after a disconnect or reboot.
- Use the phone as the default HTTPS gateway through capability-based resource
  requests.
- Use NFC field presence and a single-use enrollment token as an optional
  physical authorization path for one companion enrollment. Keep authenticated
  BLE passkey pairing as the qualified fallback.
- Start direct Wi-Fi only when policy selects it, then stop the radio.
- Qualify pairing, reconnect, background operation, security, power and
  Wi-Fi/BLE coexistence on real hardware.

Exit: firmware and Android builds pass their independent tests and shared
golden vectors. A real Android device and Note4 pass the end-to-end gate.
Power and resource measurements are recorded. A missing physical device keeps
the affected gate and Issue open.

## D1 — Maintenance CLI

Goal: provide a bounded USB maintenance interface without bypassing platform
ownership, application lifecycle or local security policy.

Status: Partially implemented against the broader GitHub scope. This milestone
can develop beside C1. The backlog's D1.1 USB Serial/JTAG
sessions and D1.2 owner-dispatched system/heap/task/uptime/display diagnostics
and bounded log observation are implemented. D1.3 adds an interactive host
simulator with terminal, pipe and reconnect integration tests. Q1 hardens USB
reconnect and shutdown. `power status`, `time get`, `connectivity status`,
`app list`, `app current`, `input watch` and confirmed mutations remain
unimplemented in the production command table. Real USB transcripts remain
required by #42–#47. Mutating connectivity commands retain the applicable C1
authorization requirements. See the [scope mapping](GITHUB_TRIAGE.md#issue-decisions).

- Use a static hierarchical command tree and bounded parser.
- Keep terminal work in the CLI task and execute platform operations through a
  fixed typed dispatcher at the application-owner safe point.
- Start with `system info`, then add copied status snapshots.
- Add bounded log and input observation with cooperative cancellation.
- Add confirmed mutations only after read-only and streaming gates pass.
- Keep Android maintenance typed; do not transport terminal text over BLE.

Exit: host, architecture, SDK and clean-build checks pass. A real Note4 passes
USB reconnect, command, cancellation, log/input stream, sleep and shutdown
tests without bypassing resource ownership.

## R1 — E-paper refresh optimization

Status: R1.1–R1.4 software delivery complete. R1.1 implements full-frame and
packed-patch comparison, minimal dirty bounds and unchanged-frame suppression
using the existing driver shadow. R1.2 adds actual black/white transition counts
and cleanup; R1.3 adds [algorithmic typography](TYPOGRAPHY.md). R1.4 replaces
the frame/accumulated-pixel scheduling limits with [spatial physics debt](DISPLAY_PHYSICS.md),
temperature/supply observations, a bounded recorder and CLI/CSV export. The
25% single-update cleanup and existing full OTP path remain. Physical optical
qualification and energy calibration remain open.

The GitHub milestone previously named R1 referred to dynamic application
research. G1.1 preserves it as **Research — Dynamic application runtime** and
creates a separate R1 milestone for this delivered refresh implementation.

The production driver, display service and demo UI pass the Host suite,
AddressSanitizer/UndefinedBehaviorSanitizer and the ESP32-S3 build. The Host
SPI capture for a clock tick sends 1,452 bytes of native RAM data; a menu
selection change sends 6,808 bytes. Both previously used a 23,400-byte fixed
window. The UI also removes a 15,000-byte patch buffer. These payload counts
do not measure panel latency or energy. Small-window appearance, ghosting
and physical refresh timing still need hardware measurement.

R1.2 Host tests cover exact pixel thresholds, repeated inversions, sparse
changes, unchanged submissions, batch cleanup, packed-patch fallback and
recovery after SPI, BUSY, power and mutex failures. A 12,000-pixel repeated
inversion now performs a full refresh on update five. The existing six-step
footprint assets change 2,312 pixels in total, below both pixel thresholds.

## Q1 — Contract regression and concurrency audits

Status: Q1.1–Q1.3 complete. The current Host suite has 35 targets. The 26-target
counts below describe the original Q1 verification.

Q1.1 adds real USB and ESP Wi-Fi driver execution to the existing Host targets.
At Q1.1 delivery the suite contained 26 targets covering application/SDK
boundaries, platform services, connectivity and sync, CLI, display and OTA
behavior. The USB stop
regression reproduced a notification sent to an already exited worker. Stop
now waits on the existing completion semaphore while the worker observes its
bounded poll interval.

| Audited boundary | Ownership and synchronization |
| --- | --- |
| Wi-Fi events and interface lifetime | One driver caller, atomic callback state, unregister synchronized with active callbacks |
| DNS completion after cancellation | Separate reference-counted query; no retained driver pointer or reuse by another burst |
| CLI inspection and cancellation | Mutex-protected slot, owner-only execution, abandoned results held until execution finishes |
| USB task and executor lifetime | Serialized lifecycle, completion semaphore, session cancellation before storage release |
| Log producers and sink replacement | Process-lifetime storage, nonblocking producer locking, atomic sink publication |

CLI and Wi-Fi regressions pass AddressSanitizer, UndefinedBehaviorSanitizer and
ThreadSanitizer. All 26 Host targets and the ESP32-S3 firmware build pass.
Their SDK fakes model callback and scheduling interleavings;
physical USB behavior, radio current and hardware coexistence need device
measurement.

Q1.2 adds explicit platform shutdown cleanup and executes the board drivers
in the existing Power Host target. It closes I2S channel leaks, joins audio
playback before codec release, synchronizes NFC callback removal and stops the
field task through its notification. EPD, I2C and audio signals are disconnected
after driver release, and deep-sleep holds retain rail-off levels on USB power.
The Wi-Fi driver tests now run complete bursts and verify ordered cleanup before
results, exclusive scan ownership and safe retention after stop/deinit/callback
cleanup failures. All 26 Host targets pass; board and Wi-Fi tests also pass
AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer, and display
tests pass AddressSanitizer/UndefinedBehaviorSanitizer. These tests do not
measure physical leakage, radio current or RF coexistence. The ESP32-S3
firmware build also passes.

Q1.3 cross-inspects Flipper Zero terminal ownership and Pebble AppMessage
delivery semantics. CLI input rejects malformed or overlong commands in full,
and USB retains transient disconnect observations until the session cancels
its old input. RX cleanup uses ESP-IDF 5.5.2 nonblocking reads with a fixed
drain bound. C++ and Kotlin replay tests interrupt every state/ACK fragment
boundary, recreate owners from storage and preserve newer pending revisions.
They also cover failed saves, queued NACKs, duplicate traffic timeouts and
exact ACK matching. All 26 Host targets, 25 Android JVM tests, the Android debug
build and the ESP32-S3 build pass. CLI and sync targets also pass
AddressSanitizer and UndefinedBehaviorSanitizer. The contracts record the
upstream references and tested behavior; real USB and BLE qualification
remains hardware work.

## L1 — Practical launcher and reader

Status: L1.1–L1.4 implemented and recorded in the L1 GitHub milestone.
The shared status bar and private SceneManager/ViewPort flow support streamed
TXT/EPUB reading, committed bookmarks, local Wi-Fi book management and static
sleep covers. See [Reader](READER.md), [Send Books](BOOK_TRANSFER.md) and
[Sleep Cover](SLEEP_COVER.md). Host, firmware and boot-smoke evidence is recorded
in those documents. Physical reading/transfer controls, sleep/wake and current
measurements remain tracked follow-ups.

## S1 — Modular platform and build profiles

Status: S1.1–S1.4 implemented. The typed service registry, Kconfig selection and
conditional application catalog support Full and Minimal firmware. Both profiles
passed the recorded flash/boot smokes. [Modular builds](MODULAR_BUILD.md) and
[RTC ownership](TIME.md) document the results and limits. Physical backup-supply
retention and standby current remain follow-up measurements.

## E1 — Daily-use UI and host communication

Status: E1.1–E1.7 implemented in [PR #54](https://github.com/Tinnci/zectrix-note4-platform/pull/54),
pending integration into main at the G1.1 review. These iterations deliver the
firmware study, Home dashboard, responsive display scheduling, unified navigation,
radio arbitration, visual refinement and system Chinese/English support.
See [Home](HOME.md), [navigation](NAVIGATION.md), [radio arbitration](RADIO_ARBITER.md)
and [localization](LOCALIZATION.md) for implementation and verification.

E1.8 implements [USB book and settings management](USB_HOST.md). An explicit
terminal command enters a bounded binary session on the existing Serial/JTAG
transport. The foreground USB Manager owns the storage lease, cancellation,
settings and progress view. A Python host tool supplies import/export, paged
listing and common settings. The architecture study compares MSC, MTP and
serial access; USB-OTG classes and an Android/browser client remain possible
extensions. Real transfer throughput and physical interaction remain device
qualification work.

## E2 — Dynamic applications

E2.1 completes the [runtime architecture study](DYNAMIC_APPLICATION_RESEARCH.md)
with actual Wasm3/WAMR/Lua execution, failure and lifecycle probes, plus isolated
ESP32-S3 Full-image links. Metered WAMR is the preferred compiled-app experiment;
Lua is a personal-scripting alternative. The report records unbounded Wasm
initialization and Host sanitizer findings, and proposes versioned host calls,
bounded app discovery, USB installation and cleanup through existing owners.

E2.2 implements the [restricted Lua pilot](MICRO_APPS.md): an optional runtime,
five-entry Apps pages, bounded initialization/events, copied clipped drawing,
USB script installation/export/removal and Calculator/Flashcards examples.
Guest errors and system exits reclaim the VM through private scenes. SDK v1
remains the source interface for static native adapters. Compiled Wasm apps
and `.zapp` packaging remain subsequent work; no new binary ABI is frozen.

## Deferred implementation

The following work is not a prerequisite for M1–M4:

- `.zapp` packaging.
- native ELF loading.
- WebAssembly runtime.
- application signing and distribution.
- custom bootloader.

E2.1 evaluates these choices and E2.2 supplies a Lua source pilot. E2.3 will
address packaging and distribution on top of the measured runtime boundary.
