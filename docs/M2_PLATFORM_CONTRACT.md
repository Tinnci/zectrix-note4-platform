# M2 platform contract

Status: Draft. This document is an internal M2 contract. It is not an SDK v1 specification.

Apply the rules in
[`PLATFORM_MIGRATION_PRINCIPLES.md`](PLATFORM_MIGRATION_PRINCIPLES.md) to each
platform migration. These rules use controlled technical English that is
aligned with useful ASD-STE100 principles. They do not state or imply formal
ASD-STE100 compliance or certification.

## Architecture boundary

Applications call platform services. Platform services call board support. Board support calls raw drivers and ESP-IDF.

| Service | Owns |
| --- | --- |
| DisplayService | logical display state, valid 1bpp baseline, dirty regions, partial refresh budget, refresh intent, error state |
| InputService | logical button mapping and event delivery |
| PowerService | sleep, deep sleep, wake, shutdown, wake reason, battery and external power |
| TimeService | RTC, wall clock and timer abstraction |
| StorageService | settings, configuration and small persistent state |
| SystemService | firmware identity, reset reason, capabilities and diagnostics |

Application code must not include `driver/gpio.h`, `driver/spi_master.h` or `zectrix_epd.h`.
Application code must not call `esp_deep_sleep_start()`, access NVS directly, or depend on PCF8563.

The checker enforces these restrictions in `main/`, `components/zectrix_demo_ui/`, and future `apps/` or `applications/` roots. Board support, platform services, raw drivers and self-test implementations are not application roots.

## M2.1a display state invariants

- Boot starts with an unknown baseline.
- Successful full 1bpp refresh makes the baseline valid and resets the partial count.
- Successful partial 1bpp refresh keeps a valid baseline and increments the partial count.
- Successful 4bpp refresh makes the baseline unknown.
- A refresh error or timeout makes the baseline unknown.
- Eight partial refreshes request a full clean refresh.
- Dirty regions are unioned until a full refresh or error clears them.

The pure state model and host tests implement M2.1a. `DisplayService` provides the M2.1b wrapper. The UI and gallery migration implements M2.1c. M2.1d remains open until the corrected firmware passes hardware regression.

`DisplayService::Create` owns the only raw driver handle. The demo UI and gallery share that service and its state. Callers use only logical display operations. A partial refresh is rejected when no valid 1bpp baseline exists or when the partial budget is exhausted. Any driver error invalidates the baseline.

`InputService` attaches to initialized board support and converts debounced physical button results to `InputEvent`. Applications use logical `Button` and `Action` values. Physical sampling, GPIO numbers, FreeRTOS queues and debounce thresholds remain inside board support.

`PowerService` exposes logical battery, external-power and wake-reason values. It owns the shutdown sequence and the deep-sleep call; board GPIO and rail control remain inside board support.
