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

The application owner stops maintenance and connectivity, attempts its final
display clear, then calls `Platform::Shutdown()`. A failed clear does not skip
peripheral cleanup. Platform destroys connectivity before detaching NFC, and
releases DisplayService before entering the final PowerService transition.
Normal destruction and initialization failures use the same service cleanup.

`PowerService::Shutdown()` first calls `ZectrixBoard::ShutdownPeripherals()`.
The board joins button sampling, closes audio, stops NFC field processing,
removes the RTC/NFC/codec I2C devices, deletes their bus and releases ADC and
button queue storage. Cleanup also disconnects I2C and audio signal pins.
The power owner then turns off the LED and audio rail, releases the battery
latch and enters deep sleep. Digital GPIO holds preserve the disabled rails
when USB keeps the ESP32-S3 powered. Cleanup errors are logged; they do not
replace the final rail-off/deep-sleep fallback.

NFC callback removal waits for any copied callback to return. The field task
exits only after receiving its stop notification, and remains event-driven
while idle. Audio self-test rounds have separate completion semaphores and
join delayed playback before returning, including after a capture or playback
timeout, so shutdown cannot delete a borrowed codec under the playback task.

`tools/test-platform.sh` verifies service release before the power transition.
`tools/test-power-service.sh` also compiles production board, audio, NFC and
self-test code with SDK fakes. It covers partial initialization, active and
closed audio, callback removal during execution, field-task stop interleaving,
delayed playback, repeated cleanup and driver resource counts at rail-off.
`tools/test-display-service.sh` verifies SPI/DMA release after failed clears
and unfinished batches, including preservation of an externally owned bus.

## Application boundary

Application code can use `Display()`, `Input()`, `Power()`, `Time()`, `Storage()`,
and `System()`.

The application gets Diagnostics through `Diagnostics()`. The Platform API does
not expose board support. Diagnostics gets typed Input, Power, Time, Storage,
and System service references during composition. Diagnostic-only audio, LED,
and NFC operations remain private to the self-test implementation.
