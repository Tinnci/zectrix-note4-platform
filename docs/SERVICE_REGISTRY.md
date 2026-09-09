# Service interfaces and registry

S1.1 adds an internal service lifecycle and typed registry in
`components/zectrix_platform/include/zectrix_service_registry.h`. The production
Platform uses it for startup, service lookup and cleanup. Existing first-party
applications use their Platform accessors, which read from the same registry.

## Interfaces and ownership

`Service` is a pure virtual base with `Init()`, `Start()` and `Stop()`, each
returning `esp_err_t`. `ServiceProvider<Interface>` adds `GetInterface()`.
The exposed type can be an abstract capability interface or an existing service
facade. Providers adapt lifecycle operations without making existing facades
inherit a new base or exposing their factories to applications.

The registry borrows providers. Their composition owner retains the providers,
interface objects and dependencies until cleanup completes. `GetInterface()`
must be side-effect-free and return the same valid pointer from successful
startup until stop. `Stop()` handles partial initialization, quiesces consumers
of borrowed dependencies, and permits owner destruction even when it reports
an error. It does not confirm firmware or enter deep sleep.

All registry operations run on the composition owner. Existing service methods
keep their own threading rules. The registry creates no task, mutex, queue or
event bus. Consumers receive a read-only registry view and cannot register or
run lifecycle operations through it. Returned pointers must not survive
provider stop. Registry destruction does not call back into borrowed objects;
the owner explicitly stops and clears before destroying providers.

## Registration and lookup

`Register(provider)` binds the provider's interface type to one of 16 fixed
slots. Register dependencies before their consumers. Duplicate interface types
or provider instances return `ESP_ERR_INVALID_STATE`; a full table returns
`ESP_ERR_NO_MEM`. Rejection leaves existing entries intact. Registration is
allowed only before startup, or after an explicit `Clear()`.

`Get<Interface>()` returns the correctly typed interface pointer, or `nullptr`
when it is absent or its provider has not completed startup. It returns
`nullptr` again before that provider's stop callback runs. Const-qualified
lookup uses the same slot. Lookups require no RTTI, string comparison, type
hash or heap allocation. Each C++ interface type has one local address across
translation units; these keys are never persisted or sent over a protocol.

The optional lookup is safe before Platform initialization and after shutdown:

```cpp
#include "zectrix_platform.h"
#include "zectrix_time_service.h"

zectrix::time::ClockSnapshot ReadClock(const zectrix::Platform& platform) {
    const auto* clock = platform.Services().Get<zectrix::time::TimeService>();
    return clock ? clock->Now() : zectrix::time::ClockSnapshot{};
}
```

`Display()`, `Input()`, `Power()`, `Time()`, `Storage()`, `System()`,
`Connectivity()`, `Update()` and `Diagnostics()` keep their reference API and
assert if used before successful Platform initialization. Their results are the
same objects returned by typed lookup. Availability describes provider lifetime,
not a live radio connection, valid RTC reading or mounted book file. For
example, `StopMaintenance()` quiesces the CLI session while its facade remains
valid until provider cleanup.

## Lifecycle

`StartAll()` runs `Init()` then `Start()` for each provider in registration
order. It publishes the interface before advancing to the next provider, so
later initialization callbacks can query ready dependencies. Calling it again
on a running registry does not repeat callbacks.

| Condition | Result |
| --- | --- |
| Init or Start fails | Stop the attempted provider, then earlier providers in reverse order; return the original error |
| Successful Start exposes a null interface | Perform the same rollback and return `ESP_ERR_INVALID_STATE` |
| A later provider was never attempted | Do not call its Stop |
| StopAll | Withdraw each interface before its Stop callback, in reverse order |
| Stop reports an error | Continue stopping other providers and return the first stop error |
| Repeated StopAll | No repeated callbacks; return `ESP_OK` |
| Register, start, clear or stop during a lifecycle callback | Return `ESP_ERR_INVALID_STATE` without interrupting the active transition |

After stop or startup failure, restarting requires `Clear()` and registration
of providers that can be initialized afresh. `Clear()` resets only the borrowed
table; it cannot clear a running registry. Production Platform retains its
existing retry rule: allocation failure before board initialization permits
retry; attempted initialization or completed shutdown requires a fresh Platform
or boot. Hardware operations are never retried automatically by the registry.

## Production composition

Ten bindings are embedded in Platform's existing `Impl` allocation. The
registry is embedded in Platform itself. Operation order remains explicit:

| Provider | Init | Start | Stop |
| --- | --- | --- | --- |
| Update | Validate boot layout and arm trial protection | Ready | Abort unfinished writer; preserve unconfirmed boot watchdog |
| Input | Initialize board, attach optional NFC adapter and Input | Ready | Withdraw Input; retain owner's handle for final board shutdown |
| Power | Attach Power | Ready | Withdraw Power; retain owner's final power handle |
| Time | Attach Time | Ready | Release facade |
| Storage | Create Storage | Initialize NVS | Release Storage and owned library resources |
| System | Attach System | Ready | Release facade |
| Display | Create Display | Ready | Release SPI/DMA and display resources |
| Diagnostics | Construct with typed service dependencies | Ready | Destroy diagnostic consumer |
| Connectivity | Create and supply Storage/NFC dependencies | Initialize connectivity | Stop/destroy connectivity before releasing its NFC adapter |
| Maintenance | Create diagnostic executor and USB CLI | Start USB CLI | Cancel dispatch, join USB session, then destroy both consumers |

If initialization stops before Connectivity runs, Platform still releases the
NFC adapter attached during board initialization. Board support and NFC remain
private and are not registered application capabilities. Shutdown withdraws
all public interfaces before the retained Power handle releases board devices,
cuts rails and sleeps. Final cover rendering still occurs before that sequence.

S1.2 will add Kconfig/CMake component selection. S1.3 will adapt application
registration and optional consumers to selected services. S1.1 supplies the
working registry and lifecycle foundation; the full build still enables the
current service set. The SDK v1 foreground lifecycle and header set are unchanged.

## Verification and references

```bash
ZECTRIX_SERVICE_SANITIZE=1 bash tools/test-service-registry.sh
ZECTRIX_PLATFORM_SANITIZE=1 bash tools/test-platform.sh
bash tools/test-host.sh
bash tools/build-firmware.sh
```

The standalone registry test compiles with RTTI and exceptions disabled. It
checks abstract interfaces, duplicate/capacity rejection, optional dependencies,
init/start/publication failures, reverse cleanup, callback reentry and stop
errors. Allocation tracking observes zero C++ heap allocations in registration,
lookup and lifecycle dispatch. The registry occupies 528 bytes on the 64-bit
Host ABI and 264 bytes on ESP32-S3, confirmed from the built ELF type information.
Existing service allocations remain owned by Platform.

Platform tests verify cross-translation-unit keys, accessor identity, lookup
withdrawal before destruction, startup ordering, every service-factory failure,
adapter allocation failures, NFC cleanup, CLI cancellation, final power handoff
and trial-boot protection. They use the production registry and composition
code with service fakes. The S1.1 iteration passed all 31 Host targets, focused
ASan/UBSan checks, ShellCheck, the ESP32-S3 firmware build and connected-device
flash/boot smoke. The device reached platform initialization, the maintenance
CLI and the Launcher runtime. Boot smoke does not exercise interactive reading,
book transfer or physical sleep/wake controls.

CrossPoint's [ActivityManager](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp)
informs explicit owner-controlled lifetime. Its reader, web-transfer and sleep
activities motivate the corresponding service boundaries. Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
inform separate lifecycle callbacks, private scene state and bounded drawing.
The service table follows the same ownership discipline on Note4's existing
owner task. No upstream application code is copied.
