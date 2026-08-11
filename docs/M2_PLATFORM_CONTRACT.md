# M2 platform contract

Status: Draft. This document is an internal M2 contract. It is not an SDK v1 specification.

Apply the rules in
[`PLATFORM_MIGRATION_PRINCIPLES.md`](PLATFORM_MIGRATION_PRINCIPLES.md) to each
platform migration. These rules use controlled technical English that is
aligned with useful ASD-STE100 principles. They do not state or imply formal
ASD-STE100 compliance or certification.

## Architecture boundary

Applications call platform services. A service can call board support or its
resource driver. Application code does not call either lower layer directly.

| Service | Owns |
| --- | --- |
| DisplayService | panel lifecycle, logical display state, valid 1bpp baseline, dirty regions, partial refresh budget, refresh intent, error state |
| InputService | logical button mapping and event delivery |
| PowerService | sleep, deep sleep, wake, shutdown, wake reason, battery and external power |
| TimeService | RTC, wall clock and timer abstraction |
| StorageService | settings, configuration and small persistent state |
| SystemService | firmware identity, reset reason, capabilities and diagnostics |

`Platform` is the composition root for these six services. It initializes board
support first. It then creates each service in a documented order. Application
code gets non-owning service references from `Platform`. Application code does
not call a service factory, call `Attach()`, or delete a service.

`Platform` destroys the services in reverse initialization order. A failed
initialization destroys each service that was already created. The same
`Platform` object does not retry initialization after a failure because board
support can be partially initialized.

Application code must not include `driver/gpio.h`, `driver/spi_master.h` or `zectrix_epd.h`.
Application code must not call `esp_deep_sleep_start()`, access NVS directly, or depend on PCF8563.
Application code must not read the application descriptor, reset reason, chip
information, flash size, heap diagnostics, or hardware MAC address directly.

The checker enforces these restrictions in the known application roots. It
also discovers service consumers from their CMake files. Board support, the
Platform composition root, platform services, raw drivers, and self-test implementations are not
application roots.

`Platform` owns Diagnostics after all six services exist. The application gets
Diagnostics from `Platform::Diagnostics()`. It does not inject board support or
individual services. Diagnostics uses InputService for button events,
PowerService for power state, TimeService for clocks and RTC, StorageService for
persistent setup, and SystemService for system identity. Diagnostic-only audio,
LED, and NFC operations stay inside the self-test implementation and board
support. They are not application API.

## M2.1a display state invariants

- Boot starts with an unknown baseline.
- Successful full 1bpp refresh makes the baseline valid and resets the partial count.
- Successful partial 1bpp refresh keeps a valid baseline and increments the partial count.
- Successful 4bpp refresh makes the baseline unknown.
- A refresh error or timeout makes the baseline unknown.
- Eight partial refreshes request a full clean refresh.
- Dirty regions are unioned until a full refresh or error clears them.

The pure state model and host tests implement M2.1a. `DisplayService` provides the M2.1b wrapper. The UI and gallery migration implements M2.1c. M2.1d remains open until the corrected firmware passes hardware regression.

`DisplayService::Create` owns the only raw driver handle. The demo UI and
gallery share that service and its state. Callers use only logical display
operations. The service owns panel power for each refresh. A caller can request
a service-managed batch when a qualified sequence must keep the panel powered.
The service rejects a partial refresh when no valid 1bpp baseline exists. When
the partial budget is exhausted, the service uses a supplied full 1bpp frame to
establish a new baseline. Any driver or panel-power error invalidates the
baseline.

`InputService` attaches to initialized board support with a typed reference. It
converts debounced physical button results to `InputEvent`. Applications use
logical `Button` and `Action` values. Wait values use native FreeRTOS ticks so
`portMAX_DELAY` keeps its wait-forever meaning. Physical sampling, GPIO
numbers, FreeRTOS queues, and debounce thresholds remain inside board support.

`PowerService` attaches to board support with a typed reference. It exposes
logical battery, external-power, and wake-reason values. It owns the shutdown
sequence and the deep-sleep call. Board GPIO and rail control remain inside
board support.

`TimeService` attaches to board support with a typed reference. It owns the
application monotonic-clock boundary, RTC calendar operations, and RTC
countdown status. Board support owns the RTC implementation, I2C operations,
and interrupt GPIO. The migration preserves the behavior in
[`M2_TIME_BASELINE.md`](M2_TIME_BASELINE.md). It does not add timezone policy or
RTC-to-system-clock synchronization.

`StorageService` owns default NVS initialization, recovery policy, and the
platform key-value namespace. Application and self-test code do not call NVS
directly. A successful write or erase commits immediately. Initialization is
on demand so the migration does not add an NVS erase path during normal boot.
The behavior baseline is in
[`M2_STORAGE_BASELINE.md`](M2_STORAGE_BASELINE.md). M2.5 does not select a
filesystem or application package format.

`SystemService` owns firmware identity, reset reason, chip capabilities, memory
diagnostics, flash size, and the hardware MAC address. Device Info and
diagnostic code get these values from the service. Board support continues to
own the detection of board capabilities such as RTC and NFC. The behavior
baseline is in [`M2_SYSTEM_BASELINE.md`](M2_SYSTEM_BASELINE.md).
