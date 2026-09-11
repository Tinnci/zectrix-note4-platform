# M3 application contract

Status: Accepted on commit `4dc371a`. This document records the M3 contract.
M4 replaced its draft public signatures with SDK v1. See `docs/SDK_V1.md`.
L1.1 updates the first-party shell and internal scene/viewport implementation;
the SDK v1 application lifecycle and public signatures remain unchanged.
L1.2 adds the Reader application and its private Library/Reading/Options scenes.
Its streamed execution and persistence are described in [READER.md](READER.md).
L1.3 adds Send Books with private Mode/Session scenes. Connectivity owns its
temporary web service and Storage lease. See [BOOK_TRANSFER.md](BOOK_TRANSFER.md).
L1.4 adds Sleep Cover with private Choose/Preview scenes and a retained final
shutdown surface. See [SLEEP_COVER.md](SLEEP_COVER.md).
S1.3 separates entry, shell ownership and application modules; a bounded
catalog supplies both enabled runtime registrations and Launcher navigation.
Clock adds a private View/Edit scene pair and TimeService-owned calibration.
E1.2 adds private Home/Tools Launcher scenes, catalog-derived tiles and a local
Continue Reading action. SDK v1 is unchanged. See [HOME.md](HOME.md).
E1.3 adds bounded input bursts through the source-compatible SDK 1.1 dispatch
API. See [DISPLAY_RESPONSIVENESS.md](DISPLAY_RESPONSIVENESS.md).
E1.4 unifies first-party button intents, restores the Launcher parent on root
Back and integrates Connectivity/Diagnostics private scenes and exit cleanup.
See [NAVIGATION.md](NAVIGATION.md). SDK signatures are unchanged.

E1.8 adds the optional USB Manager destination in Tools. It owns the existing
book-management lease and executes one copied host request per idle callback.
The USB worker has no application or Storage pointers. Settings use the same
foreground language/sleep-cover state as local controls. Binary sessions select
a one-tick shell wait; normal idle waits remain 250 ms. See [USB_HOST.md](USB_HOST.md).

E1.9 adds optional Pocket Tools with six private scenes for a focus timer,
calendar and counter. A small shell-owned RAM session survives foreground
replacement without a background application. It uses the existing TimeService
and shared canvas, with minute-only timer invalidation and no alarm or sleep
wake. Shutdown clears the session. See [UTILITIES.md](UTILITIES.md).

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

S1.1 adds an internal Platform service registry alongside the application
registry. `Services().Get<Interface>()` returns a borrowed interface or null;
it cannot navigate or create an application. Platform owns service lifecycle,
and foreground applications exit before service stop. SDK v1 is unchanged.
See [SERVICE_REGISTRY.md](SERVICE_REGISTRY.md).

S1.3's `main/app_main.cc` only calls the terminal entry. `main/terminal.cc`
owns startup, foreground dispatch, system status and shutdown;
`main/application_modules.cc` binds the selected applications after service
lookup. Concrete applications live in separate source files. Reader,
Connectivity and Send Books sources are compiled only when selected. Missing
optional services omit their destinations. One fixed 16-entry catalog stores
the descriptors and the shell owns their factories for the runtime's lifetime.
Launcher uses those same descriptors for labels and Open IDs, with no parallel
enum-to-destination table. This retains CrossPoint-style bounded reader work
and Flipper-style deferred SceneManager/ViewPort ownership.

E1.2 stores icon/Home placement beside those descriptors. Launcher preserves
Home and Tools focus in owner RAM and uses the same IDs for Open commands.
Continue Reading passes a one-shot launch mode through the composition owner;
the Reader candidate copies it before Launcher exit. The factory consumes the
mode even after failed allocation. Reader validates and opens the latest local
bookmark through its Library/Reading scenes, keeping parsing and persistence
out of Home rendering.

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

The existing `Step(event)` performs these operations in order:

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

E1.3's shell can instead call `DispatchInput(event)` for up to 16 queued
events, merging render requests before one render. OK, long presses, errors
and foreground-generation changes finish the burst immediately. The shell
never dispatches a later queued event across that boundary before rendering.
A direction-only burst ends with `Idle()`, granting bounded reader/timer work
even when more input is queued. Sampling remains on the existing button task;
application callbacks, lifecycle, maintenance polling and display calls remain
serial on the foreground owner. Status invalidation joins the same frame.
Display completion is synchronous, including Reader's `Presented()` callback.

The M3 migration adapter was removed in L1.1. Gallery, Auto Showcase, Device
Info and About now use the same foreground lifecycle as Clock, Settings,
Connectivity and Diagnostics. One runtime lives for the application shell;
opening a gallery never exits that runtime to run a separate input loop.

The Launcher-to-Clock hardware regression passed on 2026-08-12. The test
covered Launcher navigation, Clock entry and return, the old automatic test
flow, and return from the old flow. The device did not show an unexpected
reset, panic, or display failure during this test.

## M3 platform settings

The first-party shell defines these platform settings:

| Key | Type | Values | Default | Consumer |
| --- | --- | --- | --- | --- |
| `ui.auto_demo` | unsigned 32-bit integer | `0` off, `1` on | `0` (L1.1) | Launcher |
| `ui.sleep_cover` | unsigned 32-bit integer | `0` dashboard, `1` landscape, `2` blank | `0` (L1.4) | Sleep Cover / shutdown |
| `ui.language` | unsigned 32-bit integer | `0` English, `1` Simplified Chinese | Kconfig default (E1.7) | System UI |

M3 used default `1` for `ui.auto_demo`. L1.1 preserves any existing valid stored value and uses
`0` for a missing or invalid value. When enabled, the 15-second idle timeout is
measured from entry or the most recent physical input, not from idle-callback
count. Launcher selection is retained in owner RAM across application exits.

The Settings application uses `StorageService`. It does not call NVS. For
`ui.auto_demo`, a missing key creates the default value. A value outside the documented range is
invalid. Settings uses the default and attempts to replace the invalid value.
If a read or write fails, Settings remains active and shows the failure. It
does not abort or restart the device. Settings pages are private application
state and are not registry entries.

E1.7 adds the private Options -> Language scene. Boot restores `ui.language`
before the first UI frame; missing, invalid or excluded values use the compiled
default without overwriting the stored choice. OK applies and saves a language,
with a Quality redraw of content and status. A save failure retains the current
boot's choice and permits retry; an unconfirmed selection is cancelled by Back.
See [LOCALIZATION.md](LOCALIZATION.md) for the static catalog and font choices.

Sleep Cover loads `ui.sleep_cover` once after platform initialization. Missing
or invalid values use the dashboard; a missing value needs no write. OK on a
style saves it and opens Preview. A save failure shows `NOT SAVED`, applies the
choice for the current boot and permits shutdown. Selecting it again retries
the write. Its private controller uses deferred scene transitions and retries
failed preview rendering with Quality on idle callbacks.

## Diagnostics adapter

Platform owns the diagnostic engine. Diagnostics is a foreground application
that references `Platform::Diagnostics()` and does not create board support or
the engine. Its controller owns mode and test selection. The engine runs
synchronously on the application task in M3. Progress display calls therefore
also run on that task. Interactive tests can take up to 60 seconds. Their
existing cancellation loop reads `InputService` while the synchronous engine
is active. M3 does not add a worker task, queue, or event bus for this adapter.

E1.4 uses private Mode/Individual/Running/Summary scenes. Run All completion
replaces Running with Summary; OK or Back returns to Mode. Cancellation during
a test or result wait pops to its parent, preserving the individual row.
Intermediate display failure also unwinds Running. Final pages use the normal
Render callback and failed-frame retry. Root Back restores the Tools parent.

## Clock and power behavior

Clock, system status and sleep covers use `TimeService::Now()` without I2C
reads in rendering. Platform restores RTC time before networking and polls
failed restoration/persistence at most once per minute. An absent, unreadable
or invalid RTC cannot fail Clock entry. A valid local RTC without a saved
offset can be displayed, but does not establish UTC for TLS. Without any valid
calendar the view labels `TIME NOT SET` and `UPTIME` explicitly.

OK opens the Clock editor through a deferred scene push. UP/DOWN changes the
draft, OK advances and the final Save calibrates TimeService. Long OK cancels
the edit and pops to Clock, then returns to its Launcher parent from the root.
A pending RTC save is visible and does not prevent navigation or shutdown.
An authorized Companion Hello can also calibrate through the foreground platform owner.
See [TIME.md](TIME.md) for offset, failure and retention semantics.

The shell waits at most 250 ms for input between callbacks. Clock requests a
`Fast` render only when its displayed minute, date or source changes; seconds
are intentionally absent from the display. The first draw is `Quality`.
`DisplayService` can promote subsequent partial draws to a full refresh.

The shell also samples power every five seconds and time every second, and
copies current BLE/Wi-Fi state on its owner loop. A persistent status viewport
invalidates only for visible changes. Sampling and display calls remain on the
application owner; diagnostic progress callbacks and their existing bounded
waits also service this status display.

M3 does not add light-sleep suspend hooks. Shutdown ends the current application
lifecycle, so Reader saves/closes before L1.4 captures the latest committed
position. The owner stops maintenance/connectivity, draws the selected cover
and uses `Platform::Shutdown()` for cleanup and the final power transition.
Display failure attempts one white clear and cannot veto shutdown. L1.4 retains
the existing rail delays and adds a bounded released-button wait before GPIO18
wake setup and latch release. Wake starts a fresh boot and application lifecycle.
The static cover schedules no automatic idle sleep, timer wake or refresh.

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

E2.2's optional [Apps adapter](MICRO_APPS.md) uses these same rules for
List -> Loading -> Running/Error. It owns one bounded Lua guest and copied
commands inside the content viewport. Guest files are discovered in pages and
do not modify the static SDK catalog. Host-render retries reuse the completed
frame; Back and shutdown close both source handles and the guest.

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
stop image rotation and request a report. Root Back is a deferred SDK command;
explicit Home and Shutdown keep their existing higher priority.

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
shutdown releases this buffer. L1.4 shutdown replaces the live status viewport
with a static cover header, or a true white surface for Blank. The cover uses
the shared 1bpp canvas and a final FullClean commit; pending viewport work stays
dormant until normal content rendering resumes. Its clock and battery values
are snapshots, with `AS OF` marking the capture time. DisplayService ghosting,
recovery and power policies remain authoritative.

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

SDK Back and Home both open Launcher. E1.4's first-party shell distinguishes
them with a one-shot parent-return flag copied by the Launcher candidate and
consumed even after failed allocation. Back restores Home or Tools and its
focus; explicit Home and entry-failure fallback start on Home. Private child
Back is consumed by SceneManager before any shell command is submitted.

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
