# Development roadmap

The project uses stage gates. A later milestone can start only when its stated
dependencies are satisfied. Advanced E-Ink quality research runs in parallel
and does not block unrelated input or power work.

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

- Measure maximum firmware, assets and user-data requirements.
- Select an A/B OTA, rollback and recovery design.
- Record the selection in an architecture decision record.

Exit: the partition layout and update path have explicit compatibility and
recovery guarantees.

## C1 — Connectivity platform

Goal: add a BLE-first companion channel and an on-demand Wi-Fi data path
without exposing radio or RTOS mechanisms to applications.

Status: In progress. This milestone is independent of M5 and R1. The protocol,
durable synchronization, policy, secure BLE/Android path and NFC-assisted
enrollment are implemented; the remaining integration and hardware exit gates
stay open.

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

Status: In progress. This milestone can develop beside C1. Mutating
connectivity commands remain blocked by the applicable C1 authorization gate.

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

## Deferred research

The following work is not a prerequisite for M1–M4:

- `.zapp` packaging.
- native ELF loading.
- WebAssembly runtime.
- application signing and distribution.
- custom bootloader.

These items require SDK v1 and separate design decisions.
