# M3 application contract

Status: Draft. This contract controls M3 implementation. It does not freeze the
source API or ABI.

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

The registry is an immutable array view with deterministic order. Each
descriptor contains an ID, a display name, a factory, and an optional typed
factory context. The runtime validates
empty IDs, invalid IDs, duplicate IDs, missing factories, and a missing
Launcher before it creates an application. The factory receives Platform and
the read-only registry explicitly. The context implements only the generic
`ApplicationFactoryContext` marker interface. The runtime forwards it without
inspecting or owning it. The composition root must keep the context valid for
the runtime lifetime. A factory must only create an inactive candidate;
it must not perform application entry or hardware operations.

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

During M3 migration, `main` can use a private transition adapter for an
application that is not migrated. This adapter is not an `AppCommand`. The
Launcher records a value in the adapter and returns from its callback. The
composition root then ends the current lifecycle before it runs the old flow.
It creates a new Launcher lifecycle after that flow returns. Remove an adapter
entry when its application migrates.

The Launcher-to-Clock hardware regression passed on 2026-08-12. The test
covered Launcher navigation, Clock entry and return, the old automatic test
flow, and return from the old flow. The device did not show an unexpected
reset, panic, or display failure during this test.

## M3 platform settings

M3 defines one platform setting:

| Key | Type | Values | Default | Consumer |
| --- | --- | --- | --- | --- |
| `ui.auto_demo` | unsigned 32-bit integer | `0` off, `1` on | `1` | Launcher |

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

Clock reads RTC state on entry. The application shell supplies an idle input
every 15 seconds. Clock reads RTC on that input and requests one `Fast` render
only when the displayed minute or date changes. The first draw is `Quality`.
`DisplayService` can promote subsequent partial draws to a full refresh.

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
