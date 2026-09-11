# Maintenance CLI contract

Status: D1.1–D1.4 implement USB transport/session, bounded diagnostics and
observation, interactive Host simulation, system reflection and confirmed
maintenance operations. E1.8 adds
`host start 1` and the separate foreground-owned book/settings session described
in [USB_HOST.md](USB_HOST.md). Real USB transfer qualification remains open.

## Purpose

The maintenance CLI provides bounded local diagnostics and controlled device
operations through USB Serial/JTAG. It is a product maintenance interface. It
is not an operating-system shell and it is not an application SDK.

## Architecture

```text
USB Serial/JTAG
        |
    CliTransport
        |
     CliSession
        |
     CliService
  parse/help/policy
        |
PlatformControlDispatcher
 fixed typed requests/results
        |
application-owner safe point
        |
Platform services and runtime
```

The CLI task owns terminal input, line editing and output. It does not call a
platform service or `ApplicationRuntime` directly. The dispatcher wakes the
application owner and executes an accepted request only at a safe point. The
dispatcher is internal. It is not a general event bus or a public SDK API.

One resource has one owner. A command cannot bypass service state, policy or
lifecycle rules. A status result is a bounded copy. It is not a pointer or a
reference to owner state.

The ESP32-S3 transport owns the interrupt-driven USB Serial/JTAG driver for its
whole task lifetime. It shares that driver with secondary console output,
polls without an unbounded read, and restores the non-blocking console path on
clean shutdown. A physical disconnect resets the partial command and a
reconnect starts a fresh prompt. Command history is a fixed eight-entry RAM
ring and is never persisted.

One lifecycle owner serializes USB `Start()` and `Stop()` and keeps the executor
alive until `Stop()` returns. The worker checks a stop flag on its 20 ms poll,
cancels its session, restores the console path, releases USB and signals the
completion semaphore. The stop caller waits for that signal before releasing
session storage. It does not retain a worker task handle: after the flag is
set, the worker can self-delete before a separate task notification arrives.

## Command model

Commands use one immutable hierarchical descriptor tree. A descriptor defines
the command name, help, usage, access class, execution class, handler and child
descriptors. Registration is static. Runtime command plug-ins are not allowed.

Access classes:

- `ReadOnly`: The command does not change product state.
- `Confirm`: The local USB user must confirm the exact pending operation.
- `LocalConfirm`: A physical Note4 action must confirm the exact request before
  its deadline.

Origins are `UsbLocal` and `AuthorizedCompanion`. An origin does not imply an
access class. Pairing, bond removal, shutdown and destructive recovery require
the policy specified for that operation. A USB connection does not replace the
local-action rule for Bluetooth pairing.

Execution classes:

- `Immediate`: Pure CLI work that does not access platform-owned state.
- `OwnerRequest`: A bounded typed request that runs at an owner safe point.
- `Stream`: One cooperative, cancellable observation session.

## Dispatcher rules

The dispatcher uses fixed-capacity storage and POD request/result values. Each
request has an ID and a slot generation. A late result cannot complete a reused
slot. Defined results include `Ok`, `InvalidArgument`, `Denied`, `QueueFull`,
`Timeout`, `CancelledBeforeStart`, `Busy`, `Unavailable` and `UnknownOutcome`.

A timeout does not prove that an executing mutation stopped. The CLI must
report `UnknownOutcome` when completion cannot be proved. It must not retry a
non-idempotent operation automatically.

Shutdown closes admission, wakes all waiters, resolves queued work, waits for
owned execution to finish and then releases storage. The CLI must never delete
a task to cancel a command.

## Input and output

The implementation can use ESP-IDF linenoise and `esp_console_split_argv()` as
mechanisms. The ESP-IDF command registry and convenience REPL task do not define
the product architecture.

Logs use a bounded multiplexer. A log producer uses a local format buffer and
does not block indefinitely. Queue overflow increments a drop counter. Early
boot and panic output keep a direct fallback.

`input watch` receives bounded copies from an input trace tap. It does not call
`InputService::Wait()` and cannot consume an application input event.

`Ctrl+C` sets a cooperative cancellation token. A stream or long operation
checks the token and releases its resources normally.

## Resource limits

| Resource | D1 baseline |
| --- | ---: |
| Command line | 256 bytes |
| Arguments | 12 |
| Token | 64 bytes |
| Command depth | 3 |
| Format buffer | 256 bytes |
| Queued log records | 32 |
| Queued input trace records | 16 |
| Concurrent commands | 1 |
| Active streams | 1 |
| RAM-only history entries | 8 |

Command history is not persistent.

## D1 command set

```text
help [command]
version
system info
system heap
system tasks
system uptime
power status
time get
time status
time sync [unix-ms offset-seconds]
connectivity status
display status
display telemetry [after-sequence]
display model
app list
app current
scene dump
log follow [error|warn|info|debug]
log stats
input watch
reboot
sleep
storage wipe
factory reset
confirm <token>
```

All commands above are implemented. The flat aliases remain `sysinfo`,
`heap`, `tasks`, `uptime`, `epd-inspect` and `log-stream`. `app open` is not a
maintenance operation; navigation uses the existing deferred lifecycle path.
Pairing and individual bond removal remain physical actions on the device's
Connectivity screen. D1.4 deliberately does not add USB `connectivity pair` or
`connectivity forget` shortcuts: terminal confirmation is not proof of a
physical pairing action. Factory reset is a separate, explicit USB recovery
operation that also clears bonds.

E1.8's `host start 1` lends transport ownership to a bounded binary handler
when the user has opened USB Manager. It changes the transport mode only;
book/settings requests execute on that application's foreground owner. It
does not navigate from the CLI task or add generic maintenance mutations.
Logs remain captured while binary traffic owns the connection. Ctrl+C is an
ordinary byte in this mode; the host tool sends a typed Close on interruption.
Only a valid Close restores text mode. Framing errors, timeouts, local cancel
and TX failure retain binary input isolation until Close or physical reconnect.
USB polls once per RTOS tick in binary mode and keeps its 20 ms text cadence.

## D1.2 implementation

`PlatformDiagnostics` binds its dispatcher to the task that initialized
Platform. `InputService::Wait()` is a diagnostic safe point between synchronous
service calls; `Platform::PollMaintenance()` provides an explicit safe point
for owners that do not wait for input. One coalesced control notification wakes
the existing button wait. It is consumed internally, preserves the original
wait deadline and never becomes an application button or an idle/render event.
Requests arriving during a synchronous display refresh wait for its completion.

The dispatcher has one request slot, matching the single active command limit.
ID and generation checks reject old tickets. A request expires after 30 seconds.
Cancellation or timeout during inspection discards its result and keeps the
slot occupied until the owner finishes. D1.4 also admits the confirmed operations
below. The owner uses a try-lock to claim the slot and retries at subsequent safe
points after contention.
A completed result remains readable after its request deadline; a timeout
during a mutation reports `UnknownOutcome`.
Shutdown closes admission before stopping USB, then destroys the dispatcher
before the platform services. Power-off explicitly stops maintenance first.

Results are copied before formatting. Output is paged into chunks of at most
256 bytes, with one chunk per session poll. `tasks` captures at most 32 RTOS
tasks and reports a capacity overflow instead of returning an incomplete list.
It reports task IDs, state, priority, minimum remaining stack bytes and the
application owner. It never dereferences RTOS task-name or stack pointers after
sampling: another core can delete a transient task at that point. The firmware
enables the FreeRTOS trace facility needed for this inspection.

`epd-inspect` reports panel power, batch state, refresh attempts/failures, last
error and duration, partial-refresh state and dirty region. It includes the
partial frame count, accumulated changed pixels, spatial debt/budgets and the
single-update contrast threshold. Frame/pixel totals no longer set refresh
deadlines. `partial_pixels` counts transitions across successful partial
refreshes; repeated changes to the same pixels count again. Its hex dump is the
first 64 bytes of the last successful 1bpp or 4bpp frame. Partial updates copy
the existing driver shadow; full updates copy the submitted frame before its
caller releases it. Errors invalidate the preview. This command neither
retains caller buffers nor allocates an additional full framebuffer, and it
does not claim to read the physical panel pixels.

R1.4 adds `display telemetry [after-sequence]` and `display model`. The owner
copies at most four of the latest sixteen physical-attempt records into the
existing dispatcher result. The CLI formats that immutable copy as three typed
CSV rows per frame, with an exclusive continuation cursor and an overwritten
record count. `display model` reports the active coefficient dictionary and
energy calibration flags. Neither command refreshes, samples hardware or changes
model state. Records include failed attempts, actual SPI/BUSY observations and
sample ages; missing samples and uncalibrated energy remain explicit. See
[DISPLAY_PHYSICS.md](DISPLAY_PHYSICS.md) and `tools/display-telemetry.py` for the
model, capture fields and CSV conversion.

While USB maintenance runs, the ESP log callback formats into a local bounded
buffer and sends records to a 32-record ring without terminal I/O. The ring
keeps recent records, counts overwritten records and lock-contention drops,
and marks truncated text. The session alone outputs observed logs, so an idle
prompt is not interleaved with producer output. Level filtering observes logs
already enabled by ESP-IDF; it does not change the global log level. Ctrl+C,
disconnect, transport failure and shutdown cancel observation. USB TX is
nonblocking with a bounded 2 KiB pending buffer; exhaustion resets the session.
The log storage outlives the USB service, and shutdown restores the previous
sink. Early-boot and panic output retain their direct paths.

## D1.3 host implementation

`tools/run-cli-host.sh` builds and starts a POSIX terminal simulator on Linux
and macOS. `tools/build-cli-host.sh [output-binary]` builds it separately with
a C++17 compiler and no ESP-IDF dependency. `CXX` selects another compiler.
The default binary is `build-host/zectrix-cli-host`.

The simulator uses the production parser, `CliSession`, diagnostic executor,
dispatcher and display state model. A separate owner thread supplies bounded
synthetic system, heap, task and display snapshots. It updates the display
state every 250 ms and produces logs independently of terminal output. Host
results do not measure Note4 hardware or qualify the USB driver.

The stdio transport saves and restores terminal settings and descriptor flags.
Input and output use nonblocking I/O with 512-byte RX and 2 KiB TX queues.
TX exhaustion cancels the session and recovers a prompt. `Ctrl+C` uses the
production cancellation path. `Ctrl+R` discards pending input and replies and
starts a fresh session. `Ctrl+D`, terminal hangup and process termination
signals stop the owner and restore the terminal. Shutdown drains output for
at most 500 ms.

Pipes and redirected files accept one command per line. Commands wait for the
previous reply to finish. EOF submits a final line without a newline. A closed
pipe or finite file cancels log observation so subsequent commands can finish,
including input larger than the RX queue. Shell syntax is not parsed by the CLI.

`--owner-delay-ms N` delays owner safe points by 0 to 60000 ms.
`--log-interval-ms N` sets the periodic log interval from 0 to 60000 ms, where
0 disables periodic logs. `--log-burst N` emits 0 to 10000 startup records to
exercise overflow. Defaults are 0 ms owner delay, 1000 ms log interval and no
startup burst. Delayed requests do not delay cancellation, reconnect or exit.

## D1.4 reflection and input observation

`power status` copies the last foreground power sample and its age; it does
not trigger an ADC read. It reports voltage, percentage, validity, external
power and charge/full/fault/absent flags. The existing radio policy requires
20% battery unless external power is present. Standby time is explicitly
unavailable without a measured discharge model; no new battery cutoff is
invented for diagnostics.

`connectivity status` uses typed ServiceRegistry lookup and zero-timeout BLE
and resource locks. Contention returns Busy. The result separates radio
arbitration, Wi-Fi transfer state, STA/AP/Off mode and BLE encryption, bonding,
protocol negotiation and peer authorization. SSID, IP, MAC and RSSI are cached
by the existing radio owner. RSSI is an association sample, not a fresh scan;
AP mode has no station RSSI. Non-printable/non-ASCII SSID bytes become `?` in
text output. No credentials, passkeys, bond secrets or peripheral handles are
copied into the CLI result.

`app list` copies at most 16 compiled catalog entries; `app current` includes
foreground ID, generation, lifecycle and last SDK error. `scene dump` adds the
eight-entry private scene stack, per-scene state and four viewport regions
with enable/dirty/quality flags. Numeric scene IDs belong to that foreground
application. Lifecycle values are Absent=0, Creating=1, Entering=2, Active=3,
Exiting=4, Failsafe=5 and Stopped=6. SDK error values follow `sdk::Status`.

Each SceneManager publishes into shell-owned storage on its owner task.
Manager destruction clears its target; application creation clears previous
scene and guest data. The shell lends the runtime adapter only during its
runtime lifetime. Apps without private SceneManagers report depth zero. Lua
Apps also publish guest name, live/peak/rejected allocation counts, the
128 KiB heap limit and 10,000-instruction callback limit. Inspection does not
call guest code, allocate a framebuffer or retain application pointers.

`input watch` starts at the current producer cursor. The board mirrors click
and long-press events before delivery into an independent 16-record ring,
including monotonic microseconds, sequence and admission result. `queued=1`
means the application queue admitted the event, not that it was delivered:
Back/shutdown priority can later displace it. Internal maintenance wakeups
never enter this ring. Observation copies at most four records per owner
request and outputs at most one record per CLI poll. Further requests wait
at least 50 ms. Slow consumers lose the oldest trace records and receive an
explicit `lost` count. They cannot consume or reorder foreground input.
The ring is compiled out when USB CLI is disabled. Ctrl+C, reconnect, TX
failure and binary handoff retire the observer; no background reader runs.

## D1.4 confirmed operations

Issue an operation, read its scope, then enter the displayed `confirm <token>`
within 15 seconds. The executor retains the exact typed arguments and USB
origin. Any other command, malformed command, Ctrl+C, reset, disconnect or TX
failure retires the pending confirmation. Tokens increase across sessions;
there is no persistent authorization or automatic retry. The dispatcher and
owner both reject an unconfirmed mutation or a Companion-origin mutation.

| Operation | Effect after confirmation |
| --- | --- |
| `time sync <unix-ms> <offset-seconds>` | Set UTC for this boot and attempt RTC persistence; report persistence separately |
| `reboot` | Exit the foreground, stop services and restart the selected firmware |
| `sleep` | Exit the foreground, present the configured sleep cover and use normal rail-off/deep sleep |
| `storage wipe` | Clear the entire books store, including micro-apps and interrupted uploads, then reboot; retain settings |
| `factory reset` | Clear that store and the default NVS partition, including settings, Wi-Fi configuration and bonds, then reboot |

Reboot/sleep/reset inspection only schedules an action. The shell waits for
SDK callbacks to unwind, allows one second for an acceptance reply, then
calls runtime Stop. Foreground Exit closes readers, guest files, USB leases
and transfer activity before the action. A physical shutdown takes priority.
The USB reply proves acceptance, not completion. Once the owner accepts the
handoff it cannot be cancelled through Ctrl+C. Executing cancellation,
shutdown or timeout with no provable result reports unknown outcome; inspect
the device before explicitly trying again.

Data reset stops maintenance and Connectivity before touching storage.
BookStorage rejects a wipe while a reader, management lease or upload remains
open. Only the explicit wipe path may format an unreadable books filesystem;
ordinary mounting still never formats on failure. NVS erasure follows file
cleanup and successful NVS deinitialization. Cleanup failures are logged and
the device reboots without claiming a complete reset. Power loss can leave a
partial reset; it is not a transaction spanning files and NVS. Firmware slots,
OTA metadata and boot confirmation/rollback protections are preserved. The
external RTC calendar is retained, but factory reset clears its offset, so
Clock setup is needed before using that local calendar as UTC.

This follows CrossPoint's deferred activity/streamed storage cleanup and
Flipper's private scene/ViewPort ownership discussed in [NAVIGATION.md](NAVIGATION.md).
It adds no refresh, radio connection or VM callback to a status query. The
current runtime and shared 15,000-byte canvas remain the foreground owners.

D1.4 Host tests cover copied snapshots, input overflow/non-consumption,
zero-wait dispatch collisions, confirmation expiry/argument binding/replay,
queued and executing cancellation, reconnect, unknown outcomes, file leases
and reset stop order. The POSIX simulator provides synthetic reflection/input
and simulates maintenance effects without changing Host files, clock or power.
Physical USB reconnection, power-off retention and destructive recovery on a
spare device remain hardware qualification; this iteration does not close
Issues #44–#46's separate device acceptance.

Local verification passed all 37 Host targets, including 13 CLI PTY scenarios,
CLI/Platform/Time ASan/UBSan and ShellCheck. Full and Minimal ESP32-S3 builds
and profile comparison passed at 3,139,120 and 565,392 bytes respectively. Full
retains 6,608 bytes in the existing 3 MiB application slot; this iteration does
not change partitions or flash a device.

## Non-goals

- shell-language parsing, pipelines, redirection or background jobs inside the CLI;
- variables, command substitution or persistent history;
- arbitrary memory, file-system or peripheral access;
- dynamic command plug-ins;
- a BLE text terminal;
- direct task, queue, mutex, GATT, GPIO or ESP-IDF access from applications;
- direct service or runtime access from the CLI task.

Android maintenance uses typed protocol operations after peer authorization.
It does not transport CLI text.

## Required tests

Host tests cover parsing, limits, help, command arguments, copied diagnostic
values and cancellation. D1.2 tests cover submit-before-wait, the
predicate-to-block window, coalesced wakeups, queue full, timeout with a late
result, slot generation, queued/executing cancellation, shutdown during
execution, owner identity, partial/gray framebuffer previews, reconnect,
transport failure, log filtering/overflow and ESP log-sink restoration.

Q1.1 extends `tools/test-cli-diagnostics.sh` with the production USB service
over threaded RTOS/USB fakes. It reproduces worker exit before a stop
notification, verifies cancellation with blocked output, and checks cleanup
and restart after semaphore, task-creation and USB-install failures. Concurrent
log producers also run while the lifecycle owner repeatedly installs and
restores the ESP sink. The target accepts `CXX` for sanitizer compiler wrappers.

`tools/test-cli-host.sh` builds the simulator and uses Python's standard-library
pseudo-terminal and subprocess interfaces through `uv`. It verifies command
editing and limits, paged replies, ordered pipe/file input, EOF during streams,
owner progress during logging, cancellation and reconnect during owner delay,
blocked or broken output, terminal hangup, and terminal restoration on exit and
signals. `tools/test-host.sh` includes this target.

## Q1.3 terminal cross-inspection

The review uses Flipper Zero's
[CDC transport](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/cli/cli_vcp.c)
and [line editor](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/toolbox/cli/shell/cli_shell_line.c)
as references for transport ownership, backpressure, disconnect cleanup and
terminal editing. Note4 implements these properties on ESP-IDF USB Serial/JTAG
with its existing bounded owner poll.

The parser rejects an overlong line, embedded NUL or invalid byte as a whole.
It cannot execute a valid prefix hidden before rejected input. CSI/SS3 sequences
can span USB reads and consume at most 16 bytes after their introducer.
Supported cursor, Home, End, Delete and history keys edit bounded RAM state;
unsupported complete CSI/SS3 sequences are consumed without inserting their
parameters into the command. Malformed or overlong escapes reject the line;
an incomplete escape rejects submission. Ctrl+C also cancels inside an escape
or rejected line. CRLF submits once across read boundaries.

The USB transport retains any disconnect observed by a connection check until
the session owner consumes it. A link that reconnects before the next poll or
TX flush still cancels the old command, clears pending output and starts a
fresh prompt. RX cleanup uses the ESP-IDF 5.5.2 nonblocking read API, stopping
on an empty read or after 512 bytes.
It handles short reads across ring-buffer wraps and remains bounded under
continuous input. The Host USB fake uses the same public API signatures.

Core tests vary read sizes and cover rejected input and ANSI editing. The real
USB service runs over threaded fakes for short writes, overflow, queued stale
input, disconnects observed only by TX or the session, and continuous input
during cleanup. POSIX terminal tests also check that malformed or truncated
commands cannot execute a valid prefix. Device qualification must establish
which cable and terminal close/reopen events the USB Serial/JTAG connection
signal reports.

D1.4 adds input isolation, request/origin/deadline binding and mutation
unknown-outcome tests. Hardware qualification must prove USB reconnect, prompt recovery, `Ctrl+C`,
sleep/wake and shutdown without panic, watchdog or unexpected reset.
