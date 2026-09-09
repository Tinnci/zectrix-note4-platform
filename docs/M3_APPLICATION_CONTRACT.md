# M3 application contract

Status: Accepted on commit `4dc371a`. This document records the M3 contract.
M4 replaced its draft public signatures with SDK v1. See `docs/SDK_V1.md`.
L1.1 updates the first-party shell and internal scene/viewport implementation;
the SDK v1 application lifecycle and public signatures remain unchanged.
L1.2 adds the Reader application and its private Library/Reading/Options scenes.
Its streamed execution and persistence are described in [READER.md](READER.md).
L1.3 adds Send Books with private Mode/Session scenes. Connectivity owns its
temporary web service and Storage lease. See [BOOK_TRANSFER.md](BOOK_TRANSFER.md).

## Scope

M3 runs statically linked applications through one lifecycle. The runtime owns
one active foreground application. It can own one inactive candidate while it
creates the next application. It does not run background applications.

The global registry contains top-level applications only. Launcher, Clock,
Settings, and Diagnostics are top-level applications. A Settings page is
private to Settings. An application-private page ID and an `ApplicationId` are
in different namespaces. A private page must not enter the global registry.

An `ApplicationId` is a stable string value. Code must not persist a registry
index. If M3 persists an application ID, that ID becomes a persistent-data
contract immediately and requires a rename or migration policy.

The registry is an immutable array view with deterministic order. M3 used a
function factory, a Platform parameter, and a typed marker context during
validation. M4 removed these details from the public contract. SDK v1 uses a
typed factory object. The composition root owns the factory, and the runtime
receives only the inactive candidate. A factory must not perform application
entry or hardware operations.

## Task and ownership rules

One application task performs lifecycle calls, input dispatch, command
resolution, and display calls. M3 does not add a render task or a general event
bus. Platform continues to own hardware and services. Applications use public
Platform services only.

FreeRTOS is a mechanism layer below the product architecture. An application,
activity, controller, service, or navigation request is not a FreeRTOS task.
Product boundaries define lifecycle, ownership, navigation, render intent, and
power transitions. An implementation can use a task, queue, notification,
mutex, or no separate RTOS object after measurement justifies that mechanism.
Task topology must not define the product dependency graph.

M3 builds an application runtime and firmware framework on ESP-IDF and IDF
FreeRTOS. It does not build a new operating system. M4 can stabilize the
application-facing source API after real applications exercise these bounds.

An application callback can request a command or a render. It must not destroy
itself, replace itself, or call another application directly. The runtime acts
only after the callback returns.

A deferred object owns its payload. It must not contain a pointer, reference,
or `string_view` into the foreground application. `ApplicationId` uses bounded
owned storage in M3.

## Runtime step

The runtime performs these operations in order:

1. Read at most one `InputEvent`.
2. Dispatch the event to the foreground application.
3. Wait for the callback to return.
4. Resolve one deferred command.
5. Perform the lifecycle transition.
6. Validate and coalesce the pending render request.
7. Execute at most one render through `DisplayService`.

No callback-time command can destroy the object that is executing the callback.

`ApplicationRuntime` implements this sequence without a FreeRTOS task, queue,
global singleton, or heap allocation for the registry. Application factories
return explicit `esp_err_t` results and transfer one candidate pointer to the
runtime. The runtime owns that pointer immediately.

`Idle()` is an explicit runtime input. It calls the foreground idle callback.
The runtime processes a command from that callback only after the callback
returns.

The M3 migration adapter was removed in L1.1. Gallery, Auto Showcase, Device
Info and About now use the same foreground lifecycle as Clock, Settings,
Connectivity and Diagnostics. One runtime lives for the application shell;
opening a gallery never exits that runtime to run a separate input loop.

The Launcher-to-Clock hardware regression passed on 2026-08-12. The test
covered Launcher navigation, Clock entry and return, the old automatic test
flow, and return from the old flow. The device did not show an unexpected
reset, panic, or display failure during this test.

## M3 platform settings

M3 defines one platform setting:

| Key | Type | Values | Default | Consumer |
| --- | --- | --- | --- | --- |
| `ui.auto_demo` | unsigned 32-bit integer | `0` off, `1` on | `0` (L1.1) | Launcher |

M3 used default `1`. L1.1 preserves any existing valid stored value and uses
`0` for a missing or invalid value. When enabled, the 15-second idle timeout is
measured from entry or the most recent physical input, not from idle-callback
count. Launcher selection is retained in owner RAM across application exits.

The Settings application uses `StorageService`. It does not call NVS. A
missing key creates the default value. A value outside the documented range is
invalid. Settings uses the default and attempts to replace the invalid value.
If a read or write fails, Settings remains active and shows the failure. It
does not abort or restart the device. Settings pages are private application
state and are not registry entries.

## Diagnostics adapter

Platform owns the diagnostic engine. Diagnostics is a foreground application
that references `Platform::Diagnostics()` and does not create board support or
the engine. Its controller owns mode and test selection. The engine runs
synchronously on the application task in M3. Progress display calls therefore
also run on that task. Interactive tests can take up to 60 seconds. Their
existing cancellation loop reads `InputService` while the synchronous engine
is active. M3 does not add a worker task, queue, or event bus for this adapter.

## Clock and power behavior

Clock reads RTC state on entry and at most once per second on idle callbacks.
An absent, unreadable or invalid RTC does not fail application entry. It logs
a warning on entry or RTC loss and uses `TimeService::Now()`: a valid system
calendar first, otherwise monotonic uptime with `TIME NOT SET` and `UPTIME`
labels. A recovered RTC is used on the next sample. System fallback uses UTC
or the explicit offset from the last successful RTC-to-system synchronization.
The fallback does not set the system clock or weaken TLS clock requirements.

The shell waits at most 250 ms for input between callbacks. Clock requests a
`Fast` render only when its displayed minute, date or source changes; seconds
are intentionally absent from the display. The first draw is `Quality`.
`DisplayService` can promote subsequent partial draws to a full refresh.

The shell also samples power every five seconds and time every second, and
copies current BLE/Wi-Fi state on its owner loop. A persistent status viewport
invalidates only for visible changes. Sampling and display calls remain on the
application owner; diagnostic progress callbacks and their existing bounded
waits also service this status display.

M3 does not add light-sleep suspend hooks. Qualified shutdown ends the current
application lifecycle, clears the display, preserves the established rail
timing, and enters deep sleep through `PowerService`. Wake starts a fresh boot
and a fresh application lifecycle. This is the M3 suspend/resume boundary.

## Lifecycle

The normal lifecycle is:

```text
Absent -> Creating -> Entering -> Active -> Exiting -> Absent
```

Create the candidate before exit from the active application. If candidate
creation fails, keep the active application. This rule limits disruption when
allocation fails.

```text
Active(A) -- Open(B) --> Create candidate B
    |                       |
    |                       +-- failure/OOM --> Active(A)
    |                       |
    |                       +-- success --> Exit(A) --> Destroy(A)
    |                                         |
    |                                         +--> Enter(B)
    |                                                |
    |                                                +-- success --> Active(B)
    |                                                +-- failure --> Destroy(B)
    |                                                                  |
    +<---------------------- launch Launcher <--------------------------+
```

If Launcher creation or entry fails, enter a low-allocation system failsafe.
The failsafe must preserve shutdown and recovery access. It must not allocate a
normal application object.

`Exit()` is idempotent and cannot veto destruction. It should not throw. If it
reports an error, the runtime records the error, destroys the application once,
and continues the transition. Repeated Stop or Shutdown calls must not destroy
an object twice.

Entry failures are logged with the application ID, SDK status and fallback
destination before cleanup. A failed Clock RTC read uses the Clock fallback
above, rather than triggering the application-entry failure path.

## L1.1 private scenes and viewports

`SceneManager` uses a static handler table and fixed storage for eight private
scene IDs, eight stack entries and one `uint32_t` state value per scene. Only
an event callback can request Push, Replace or Pop. A request validates its
target and capacity before any exit; the first request wins. The transition
executes after the callback returns. Enter/exit callbacks and recursive event
dispatch cannot trigger another transition. An unconsumed Back pops a child;
at the root it propagates to the foreground application. Stop exits only the
active scene, because suspended parents already received Exit when pushed.

Gallery uses Menu -> Preview -> Report, with Replace for Preview -> Report so
Back returns to the menu and its saved selection. Rendering remains in the SDK
Render callback. Animation advances at most one frame per idle event and
starts its next deadline after physical display completion. Display errors
stop image rotation and request a report. Shutdown and
root Home are SDK commands and keep their existing priority.

`ViewPortScheduler` holds at most four bounded screen regions, with optional
draw callbacks and dirty/quality flags. The shell uses two: status at
`(0,0,400,24)` and content at `(0,24,400,276)`. Canvas drawing is clipped to the
selected region. Composition coalesces invalidations and Quality dominates
Fast; failed commits retain pending work for recovery. The shared 15,000-byte
1bpp canvas retains content during status-only refreshes. The normal page
render and a pending status update share one display commit.

A gray preview temporarily owns one 60,000-byte 4bpp buffer. Its header is
composed from the status viewport, so a status update cannot replace its image
with an old monochrome canvas. Gray refreshes retain the established white
1bpp preclear followed by a full 4bpp refresh. Returning to a normal page or
shutdown releases this buffer. Shutdown clears a true white surface without
the status overlay. The DisplayService ghosting, recovery and power policies
remain authoritative.

## Command arbitration

Command priority is:

```text
Shutdown > Home > Open or Back > Render
```

A higher-priority command supersedes a pending lower-priority command. The
first command wins when two mutually exclusive commands have the same priority.
The runtime returns `Conflict` for the second command and ignores it.

| Pending | New request | Result |
| --- | --- | --- |
| none | Open A | accept Open A |
| Open A | Back | keep Open A; Conflict |
| Open A | Open B | keep Open A; Conflict |
| Open A | Home | replace with Home |
| Home | Open A | keep Home; Conflict |
| any non-shutdown | Shutdown | replace with Shutdown |

Only the foreground application callback can submit an application navigation
command. Services and generic event publishers cannot navigate applications.

## Render requests

Applications express `Fast` or `Quality` intent and a dirty region.
`DisplayService` selects partial or full refresh, waveform, and baseline
recovery. An application does not select a waveform.

Each request contains the foreground generation. The runtime increments the
generation when it replaces the foreground application. It discards a request
whose generation is not current.

| Pending request | New request | Result |
| --- | --- | --- |
| none | Fast(R1) | Fast(R1) |
| Fast(R1) | Fast(R2) | Fast(union R1,R2) |
| Fast(R1) | Quality(R2) | Quality(union R1,R2) |
| Quality(R1) | Fast(R2) | Quality(union R1,R2) |
| Quality(R1) | Quality(R2) | Quality(union R1,R2) |
| outgoing generation | navigation | discard outgoing request |
| generation N | current generation N+1 | discard request |

Home discards the outgoing application request. Shutdown follows the qualified
shutdown sequence and can render only the final shutdown surface specified by
that sequence. M3 does not provide `RenderAndWait()`.

An application composition failure occurs before a physical display commit. It
does not change the panel baseline. A failure after `DisplayService` starts the
physical update makes the physical state uncertain; `DisplayService` owns that
baseline transition.

## Required failure tests

Tests must cover:

- callback-time Open and Home without callback-time destruction;
- owned command payload after the source application is destroyed;
- empty, duplicate, and unknown application IDs;
- factory null and injected allocation failure;
- candidate creation failure while the current application stays active;
- Enter failure, Launcher fallback, and failsafe entry;
- Exit error followed by exactly one destruction;
- repeated Stop and Shutdown;
- all command-priority and same-priority conflict combinations;
- render dirty-region union and `Quality` dominance;
- stale-generation render rejection;
- composition failure without baseline invalidation;
- physical refresh failure with DisplayService baseline invalidation;
- peak heap during candidate creation, entry, failure cleanup, and fallback.

## Exclusions

M3 does not add dynamic ELF, WebAssembly, `.zapp`, linker-section registration,
global-constructor registration, a plugin store, application tasks, a render
task, a compositor, a retained widget tree, arbitrary top-level navigation
stacks, a general permission system, OTA changes, partition changes, or API/ABI
compatibility promises.
