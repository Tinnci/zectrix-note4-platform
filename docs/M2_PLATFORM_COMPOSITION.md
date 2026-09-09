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

## Initialization

`Platform::Initialize()` performs these operations:

1. Initialize board support.
2. Attach InputService.
3. Attach PowerService.
4. Attach TimeService.
5. Create StorageService.
6. Attach SystemService.
7. Create DisplayService.
8. Create Diagnostics with typed references to the services.

The order preserves the qualified demo startup behavior. Display initialization
remains the last service operation before the splash screen.

If an operation fails, `Platform` destroys each service that it already created.
It destroys the services in reverse order. It does not retry board initialization
on the same object.

Allocation failure returns `ESP_ERR_NO_MEM`. The application calls service
accessors only after `Initialize()` returns `ESP_OK`. An accessor asserts this
precondition before it returns a reference.
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
