# Zectrix SDK v1

Status: Source-stable version 1.0.0.

## Scope

SDK v1 controls statically linked foreground applications. It defines input,
lifecycle, navigation, render intent, status, factories, the registry, and the
single-foreground runtime. It does not define a UI toolkit, hardware driver
API, dynamic loader, binary application format, or binary ABI.

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
zectrix/sdk/version.h
zectrix/zectrix_sdk.h
```

## Version policy

SDK v1 uses semantic version 1.0.0.

- Increment `major` for a source-breaking change.
- Increment `minor` for an additive source-compatible feature.
- Increment `patch` for a compatible correction.
- Keep deprecated source available until the next major version.
- Mark a deprecated item in the header and in this document.
- Give the replacement and the planned removal major version.

SDK version numbers do not describe firmware image compatibility, persistent
data format, OTA compatibility, or a binary application ABI.

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

Commands are deferred. Their priority is:

```text
Shutdown > Home > Open or Back > Render
```

Each render request contains an owned dirty region, a `Fast` or `Quality`
intent, and the foreground generation. The runtime coalesces at most one
pending request. It discards a request from an outgoing generation. The display
service selects the physical refresh mode and owns E-Ink baseline recovery.

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
