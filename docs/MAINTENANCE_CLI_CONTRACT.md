# Maintenance CLI contract

Status: D1.1 USB transport/session, D1.2 read-only platform diagnostics and
log observation, and D1.3 interactive host simulation are implemented. Input
tracing and mutating commands remain future slices. Real USB qualification
remains open.

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
connectivity status
connectivity pair
connectivity forget
display status
app list
app current
log follow [error|warn|info|debug]
log stats
input watch
```

Implemented commands are `help [command]`, `version`, `system info`,
`system heap`, `system tasks`, `system uptime`, `display status`,
`log follow [error|warn|info|debug]` and `log stats`. The flat aliases are
`sysinfo`, `heap`, `tasks`, `uptime`, `epd-inspect` and `log-stream`.
Other commands in the list above are future work. `app open` is not in D1
because runtime switching must use the existing deferred lifecycle path.

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
slot occupied until the owner finishes. Only read-only inspections execute in
D1.2; the mutation outcome and confirmation rules above apply to future work.
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
partial frame count, accumulated changed pixels and the single-update contrast
threshold. `partial_pixels` counts transitions across successful partial
refreshes; repeated changes to the same pixels count again. Its hex dump is the
first 64 bytes of the last successful 1bpp or 4bpp frame. Partial updates copy
the existing driver shadow; full updates copy the submitted frame before its
caller releases it. Errors invalidate the preview. This command neither
retains caller buffers nor allocates an additional full framebuffer, and it
does not claim to read the physical panel pixels.

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

Future input tracing, mutations and local confirmation need the corresponding
event isolation, request/origin/deadline binding and unknown-outcome tests.
Hardware qualification must prove USB reconnect, prompt recovery, `Ctrl+C`,
sleep/wake and shutdown without panic, watchdog or unexpected reset.
