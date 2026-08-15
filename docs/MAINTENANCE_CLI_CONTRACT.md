# Maintenance CLI contract

Status: Draft for D1 implementation.

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

The first vertical slice is `system info`. Read-only status commands follow.
Streaming commands follow the log multiplexer and input trace. Mutating
commands are last. `app open` is not in D1 because runtime switching must use
the existing deferred lifecycle path.

## Non-goals

- shell scripts, pipes, redirection or background jobs;
- variables, command substitution or persistent history;
- arbitrary memory, file-system or peripheral access;
- dynamic command plug-ins;
- a BLE text terminal;
- direct task, queue, mutex, GATT, GPIO or ESP-IDF access from applications;
- direct service or runtime access from the CLI task.

Android maintenance uses typed protocol operations after peer authorization.
It does not transport CLI text.

## Required tests

Host tests cover parsing, limits, help, access policy and cancellation. The
dispatcher tests cover submit-before-wait, the predicate-to-block window,
coalesced wakeups, queue full, FIFO order, timeout with a late result, slot
generation, queued cancellation, executing mutation outcome, shutdown during
submit and owner-task identity.

Integration tests prove that runtime requests wait for a safe point, display
status is copied, input tracing does not consume events, local confirmation is
bound to one request/origin/deadline, and logs report overflow. Hardware tests
prove USB reconnect, prompt recovery, `Ctrl+C`, sleep/wake and shutdown without
panic, watchdog or unexpected reset.
