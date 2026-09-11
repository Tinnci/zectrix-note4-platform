# M2 Platform composition baseline

Status: Draft. This baseline applies to issue #16. It does not freeze the source
API or ABI.

## Ownership

One `Platform` object owns board support and these services:

1. `InputService`
2. `PowerService`
3. `TimeService`
4. `StorageService`
5. `SystemService`
6. `DisplayService`

The application owns only the `Platform` object. It gets non-owning references
from `Platform`. It does not create, attach, or delete a service.

S1.2 registers mandatory BootGuard and Diagnostics alongside these six core
services, plus selected Update, Connectivity and Maintenance providers.
`Platform::Services()` exposes a read-only typed registry; absent or stopped
providers return `nullptr`. Eight to eleven lifecycle bindings live in the
existing Platform implementation allocation. Details are in
[SERVICE_REGISTRY.md](SERVICE_REGISTRY.md).

## Initialization

`Platform::Initialize()` performs these operations:

1. Validate boot layout and arm trial-boot protection; hand known-good boots to runtime health and expose the optional firmware writer.
2. Initialize board support and attach the NFC enrollment adapter when Connectivity is selected.
3. Attach InputService.
4. Attach PowerService.
5. Create StorageService; report NVS initialization failure as degraded settings without erasing data.
6. Attach TimeService and restore RTC wall time using the stored offset; an unset/failed RTC is nonfatal.
7. Attach SystemService and sample the reset reason for recovery-boot policy.
8. Create DisplayService.
9. Create Diagnostics with typed references to the services.
10. Create Connectivity with Storage/NFC dependencies when selected; keep its facade stopped when settings are unavailable or the reset reason is panic/watchdog.
11. Create the maintenance executor and start the USB CLI when selected.

The registry runs each provider's `Init()` and `Start()`, publishes its
interface, then advances to the next provider. S1.3 starts Storage before Time
so UTC is restored before networking. Display remains after core board services
and before its consumers. Time is released before its borrowed Storage handle.

If a required operation fails, `Platform` destroys each service that it already created.
It withdraws interfaces and stops attempted providers in reverse order,
including a provider whose initialization only partly completed. Power and
Input handles remain with the owner until final board cleanup. It does not
retry board initialization on the same object.

Allocation failure returns `ESP_ERR_NO_MEM`. The application calls service
accessors only after `Initialize()` returns `ESP_OK`. An accessor asserts this
precondition before it returns a reference. The optional
`Services().Get<Interface>()` path is safe before initialization and after stop.
If allocation fails before board initialization starts, the application can
retry initialization on the same Platform object.

## Product shutdown

The runtime exits its foreground application before the application owner
stops maintenance and connectivity. L1.4 captures time, power and the latest
committed reader bookmark, presents the selected sleep cover, then calls
`Platform::Shutdown()`. A failed cover attempts one white clear; neither failure
skips peripheral cleanup. Pending status draws are suppressed on the final
surface. Platform destroys connectivity before detaching NFC, and
releases DisplayService before entering the final PowerService transition.
Normal destruction and initialization failures use the same service cleanup.
The registry retires each interface before its stop callback releases it, then
clears all borrowed bindings before Platform destroys its implementation.
Stopping Update aborts an unfinished writer without confirming a trial image or
disarming its unconfirmed boot watchdog.

The foreground health supervisor remains armed during service/board cleanup.
PowerService's final owner hook disarms the runtime watchdog only after devices
have been released and button wake prepared, before cutting battery power or
sleeping. Reboot disarms it after services stop, immediately before reset.
Ordinary destruction and failed initialization never disarm either watchdog.

`PowerService::Shutdown()` first calls `ZectrixBoard::ShutdownPeripherals()`.
The board joins button sampling, closes audio, stops NFC field processing,
removes the RTC/NFC/codec I2C devices, deletes their bus and releases ADC and
button queue storage. Cleanup also disconnects I2C and audio signal pins.
The power owner then turns off the LED and audio rail, waits the existing
100 ms, prepares button wake, releases the battery latch, waits the existing
100 ms and enters deep sleep. Digital GPIO holds preserve the disabled rails
when USB keeps the ESP32-S3 powered. Cleanup errors are logged; they do not
replace the final rail-off/deep-sleep fallback.

Board wake preparation waits for three released DOWN samples at 20 ms
intervals, bounded to 250 polls (about five seconds). It then configures GPIO18
as RTC input with pull-up and EXT1 ANY_LOW wake. This avoids immediately waking
from a held shutdown button without forcing the RTC peripheral power domain
on. A stuck button or setup error still reaches rail-off; USB sleep then needs
reset/power cycling. Boot releases GPIO18's RTC hold and mode before normal
button setup. L1.4 adds no timer wake. See [SLEEP_COVER.md](SLEEP_COVER.md).

NFC callback removal waits for any copied callback to return. The field task
exits only after receiving its stop notification, and remains event-driven
while idle. Audio self-test rounds have separate completion semaphores and
join delayed playback before returning, including after a capture or playback
timeout, so shutdown cannot delete a borrowed codec under the playback task.

`tools/test-platform.sh` verifies service release before the power transition.
`tools/test-power-service.sh` also compiles production board, audio, NFC and
self-test code with SDK fakes. It covers partial initialization, active and
closed audio, callback removal during execution, field-task stop interleaving,
delayed playback, repeated cleanup, button release/wake failures and driver
resource counts at rail-off. `tools/test-display-service.sh` verifies final
cover retention through SPI/DMA release, privacy clearing, failed-cover
fallback and unfinished batches, including preservation of an externally
owned bus. Physical sleep/wake and current remain hardware measurements.

## Application boundary

Application code can use `Display()`, `Input()`, `Power()`, `Time()`, `Storage()`,
and `System()`.

The application gets Diagnostics through `Diagnostics()`. The Platform API does
not expose board support. Diagnostics gets typed Input, Power, Time, Storage,
and System service references during composition. Diagnostic-only audio, LED,
and NFC operations remain private to the self-test implementation.
