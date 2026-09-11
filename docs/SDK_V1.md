# Zectrix SDK v1

Status: Source-stable version 1.2.0.

## Scope

SDK v1 controls statically linked foreground applications. It defines input,
lifecycle, navigation, render intent, status, factories, the registry, and the
single-foreground runtime. It does not define a UI toolkit, hardware driver
API, dynamic loader, binary application format, or binary ABI.

The optional [Lua micro-app pilot](MICRO_APPS.md) is a private native Apps
adapter using this lifecycle. Its script functions, quota allocator and copied
frame interface are separate from SDK v1. Version 1.2 adds a shared style
vocabulary for text; application lifecycle signatures retain their behavior.

Include the umbrella header:

```cpp
#include "zectrix/zectrix_sdk.h"
```

The minimal application is in `examples/sdk_v1_minimal_app.cc`.

## Compatibility classes

| Class | Interfaces | Promise |
| --- | --- | --- |
| Source-stable | `zectrix/sdk/*.h`, `zectrix/zectrix_sdk.h` | Compatible source continues to compile within major version 1. Rebuild is required. |
| Internal | Platform and service headers, first-party controllers, UI, board support, raw EPD, self-test, and implementation files | These interfaces can change with the firmware. |
| ABI-stable | None | SDK v1 does not promise binary compatibility. |

The exact source-stable header set is:

```text
zectrix/sdk/application.h
zectrix/sdk/input.h
zectrix/sdk/status.h
zectrix/sdk/text_style.h
zectrix/sdk/version.h
zectrix/zectrix_sdk.h
```

## Version policy

SDK v1 uses semantic version 1.2.0.

- Increment `major` for a source-breaking change.
- Increment `minor` for an additive source-compatible feature.
- Increment `patch` for a compatible correction.
- Keep deprecated source available until the next major version.
- Mark a deprecated item in the header and in this document.
- Give the replacement and the planned removal major version.

SDK version numbers do not describe firmware image compatibility, persistent
data format, OTA compatibility, or a binary application ABI.

## Text style vocabulary

Version 1.2 adds the one-byte `TextStyle` enum: `Regular`, `Bold`, `Italic`,
`Dim`, `Underline` and `Keycap`, with constexpr `|`, `&` and `HasStyle` helpers.
`HasStyle(value, flags)` tests whether any requested bit is present.
Styles describe intent; dense CJK can express emphasis through underlining.
Applications must measure with the same style they draw. See
[TYPOGRAPHY.md](TYPOGRAPHY.md) for the first-party rendering rules.

`text_style.h` is in `components/zectrix_text/include/zectrix/sdk/`; the other
public headers remain in `components/zectrix_app/include/`. ESP-IDF propagates
both include roots through the app component dependency. Standalone consumers
add both include roots, as `tools/test-sdk-v1.sh` demonstrates. The SDK umbrella
includes the vocabulary, without exposing a UI toolkit or making rendering
implementation headers source-stable.

## Ownership and lifetime

The composition root owns each `ApplicationFactory`. A factory must remain
valid for the complete registry and runtime lifetime. A descriptor refers to a
factory and does not own it.

The composition root also owns the descriptor array and the null-terminated ID
and display-name strings. They must remain valid for the registry and runtime
lifetime. The registry is a read-only array view. It does not allocate or copy
descriptor metadata.

A successful factory call transfers one inactive `Application` to the runtime.
The runtime destroys it exactly once. A failed factory call must leave `output`
null. The factory must not enter the application or access hardware as part of
creation.

The runtime owns the active `Application`. It owns command payloads and render
requests. An application must not retain `ApplicationContext`, a callback input
reference, or a render-request reference after the callback returns.

## Lifecycle and execution context

The runtime invokes callbacks serially on the caller's execution context:

```text
Create -> Enter -> HandleEvent or HandleIdle -> Render -> Exit -> Destroy
```

SDK v1 does not create a task. It is not thread-safe. The caller must not call
one runtime concurrently from multiple tasks. An application must not create,
destroy, or replace itself. It requests navigation through `AppCommand`, and
the runtime acts after the callback returns.

An application callback can return a failure. The runtime records the failure.
A render failure does not destroy the active application. A failed candidate
creation keeps the current application active. A failed candidate entry causes
a Launcher fallback. A Launcher failure enters the firmware failsafe.

`Exit()` cannot veto destruction. It must be safe to call once after a partial
entry. It must not throw. Shutdown exits and destroys the foreground
application before the delegate performs the platform shutdown.

## Input, commands, and rendering

`InputEvent` contains a product button and action. It contains no GPIO number,
interrupt value, debounce state, or RTOS tick.

The first-party shell's [navigation conventions](NAVIGATION.md) map hold OK to
one-level Back and hold DOWN to shutdown. SDK Back and Home both target
Launcher; the shell restores its Home/Tools parent only for Back. Private
scenes consume child Back before submitting an application command. These are
first-party policies and do not change SDK command signatures or priority.

Commands are deferred. Their priority is:

```text
Shutdown > Home > Open or Back > Render
```

Each render request contains an owned dirty region, a `Fast` or `Quality`
intent, and the foreground generation. The runtime coalesces at most one
pending request. It discards a request from an outgoing generation. The display
service selects the physical refresh mode and owns E-Ink baseline recovery.

Version 1.1 adds `ApplicationRuntime::DispatchInput(const InputEvent&)`. It
dispatches one event and resolves its command after the callback returns,
without invoking `Render`. Existing `Step(const InputEvent*)` retains its
signature and dispatch/command/render behavior. `Step()` flushes pending work;
`Idle()` gives the foreground an idle callback before flushing it.

A caller using `DispatchInput` must bound each burst and finish with `Step()`
or `Idle()` before blocking for new input. Check lifecycle state and foreground
generation after every dispatch: shutdown needs no flush, and a transition
must finish drawing its new foreground before dispatching more input.
Confirmation and long presses also end the first-party shell's burst. A
direction-only burst finishes with `Idle()` so pagination and timers continue
under load. Dirty regions merge and Quality wins within the pending request;
requests belonging to an outgoing foreground are discarded. This API does not
represent a successful physical display: bookmarks and display accounting
still depend on synchronous render completion. Callback reentry through
`Start`, `Step`, `DispatchInput`, `Idle` or `Stop` returns `InvalidState`.
Version 1.1.1 extends this protection to factories, application destructors
and shutdown/failsafe delegates. Navigation and render submission are accepted
only from entering or active application callbacks. A factory or cleanup
callback cannot start another lifecycle operation while ownership is changing.
See [DISPLAY_RESPONSIVENESS.md](DISPLAY_RESPONSIVENESS.md) for the shell policy.

## Errors

SDK v1 uses `Status`. It does not carry ESP-IDF error numbers.

- `InvalidArgument`: the caller supplied an invalid value or null output.
- `InvalidState`: the operation is not valid in the current lifecycle state.
- `NotFound`: a requested application ID does not exist.
- `NoMemory`: object creation failed.
- `Busy` or `Conflict`: the request cannot be accepted now.
- `IoError`: a platform operation failed.
- `Timeout`: a bounded operation expired.
- `Unsupported`: the platform does not implement the request.
- `InternalError`: the runtime or application detected an invariant failure.

The firmware composition root converts platform-specific failures to `Status`.
Application source must not compare an SDK status with an ESP-IDF value.

## Capabilities

SDK v1 has no generic capability query. The static composition root registers
only applications whose required private dependencies exist. An application
must not infer a capability from a chip type or driver handle.

Add a capability API only when two real consumers need optional behavior. Add
new capability values without changing the meaning of existing values. An
unknown future capability must be safe to ignore.

## Restrictions

Do not expose RTOS objects in an SDK header. Do not make an application or
controller equal to a task. Do not call application callbacks concurrently.
Do not add a direct call from one application to another. Do not persist a
registry index. Do not use an SDK version as a firmware or storage-format
version.
