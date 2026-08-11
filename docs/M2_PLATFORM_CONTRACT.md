# M2 platform contract

Status: Draft. This document is an internal M2 contract. It is not an SDK v1 specification.

## Architecture boundary

Applications call platform services. Platform services call board support. Board support calls raw drivers and ESP-IDF.

| Service | Owns |
| --- | --- |
| DisplayService | logical display state, valid 1bpp baseline, dirty regions, partial refresh budget, refresh intent, error state |
| InputService | physical button to logical event mapping and debounce |
| PowerService | sleep, deep sleep, wake, shutdown, wake reason, battery and external power |
| TimeService | RTC, wall clock and timer abstraction |
| StorageService | settings, configuration and small persistent state |
| SystemService | firmware identity, reset reason, capabilities and diagnostics |

Application code must not include `driver/gpio.h`, `driver/spi_master.h` or `zectrix_epd.h`.
Application code must not call `esp_deep_sleep_start()`, access NVS directly, or depend on PCF8563.

The current demo has migration exceptions in `main/` and `components/zectrix_demo_ui/`. Board support, raw drivers and self-test code are not application roots. Run `tools/check-architecture-boundaries.sh` to check future `apps/` and `applications/` roots.

## M2.1a display state invariants

- Boot starts with an unknown baseline.
- Successful full 1bpp refresh makes the baseline valid and resets the partial count.
- Successful partial 1bpp refresh keeps a valid baseline and increments the partial count.
- Successful 4bpp refresh makes the baseline unknown.
- A refresh error or timeout makes the baseline unknown.
- Eight partial refreshes request a full clean refresh.
- Dirty regions are unioned until a full refresh or error clears them.

The pure state model and host tests implement M2.1a. The service wrapper, UI migration and hardware regression remain separate M2.1b-d work.
