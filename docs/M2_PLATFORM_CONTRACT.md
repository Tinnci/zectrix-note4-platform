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
support can be partially initialized. If allocation fails before board
initialization starts, the same object can retry safely.

`Platform::Initialize()` reports allocation failure as `ESP_ERR_NO_MEM` and
cleans up completed service creation. An application must get service
references only after `Initialize()` returns `ESP_OK`. Access before successful
initialization is a contract violation and triggers an assertion.

Application code must not include `driver/gpio.h`, `driver/spi_master.h` or `zectrix_epd.h`.
Application code must not call `esp_deep_sleep_start()`, access NVS directly, or depend on PCF8563.
Application code must not read the application descriptor, reset reason, chip
information, flash size, heap diagnostics, or hardware MAC address directly.

The checker scans `main`, application directories, and every first-party
component that is not in the explicit infrastructure allowlist. Discovery does
not depend on CMake dependency text. It rejects application use of the board
header or `ZectrixBoard` type. Board support, the Platform composition root,
platform services, raw drivers, and self-test implementations are in the
infrastructure allowlist.

`Platform` owns Diagnostics after all six services exist. The application gets
Diagnostics from `Platform::Diagnostics()`. It does not inject board support or
individual services. Diagnostics uses InputService for button events,
PowerService for power state, TimeService for clocks and RTC, StorageService for
persistent setup, and SystemService for system identity. Diagnostic-only audio,
LED, and NFC operations stay inside the self-test implementation and board
support. They are not application API.

## M2.1a display state invariants

- Boot starts with an unknown baseline.
- Successful full 1bpp refresh makes the baseline valid and resets the partial frame and pixel counts.
- Successful partial 1bpp refresh keeps a valid baseline and adds one frame and its actual changed-pixel count.
- An unchanged `Auto` or `Fast` submission with a valid baseline does not refresh or change counters.
- Successful 4bpp refresh makes the baseline unknown.
- A refresh error or timeout makes the baseline unknown.
- After eight partial refreshes, the next changed submission requests a full clean refresh.
- Large single-frame changes or accumulated pixel transitions can require a full refresh sooner.
- Actual changed pixel bounds are unioned until a full refresh or error clears them.

The pure state model and host tests implement M2.1a. `DisplayService` provides
the M2.1b wrapper. The UI and gallery migration implements M2.1c. The M2.1
acceptance is open until the intent API passes hardware regression.

`DisplayService::Create` owns the only raw driver handle. The demo UI and
gallery share that service and its state. Callers submit `DisplayIntent` and
frame data. They do not call a raw refresh operation. `Auto` and `Fast` compare
the supplied full 1bpp frame with the driver's existing shadow. They use a
partial refresh of the smallest changed bounding box when the baseline and
partial budget permit it. The driver expands the horizontal window to byte
boundaries while preserving neighboring pixels.

The existing explicit packed-patch form remains supported. In that form only
the patch is compared and applied; the supplied full frame remains the
fallback when a full refresh is required. Malformed patch arguments are
rejected before panel operations. The demo UI submits its complete canvas,
including the header and footer, without a separate patch buffer.

An unchanged `Auto` or `Fast` submission returns without panel power changes,
refresh operations, inspection-counter changes or partial-budget use. This
also applies after eight partial refreshes. An unknown service state or an
invalid driver shadow requires recovery with the supplied full frame, even
when the pixels appear unchanged. `Quality` and `FullClean` always use the
full 1bpp path. A 4bpp frame accepts `Quality` only. The service owns panel power
for each refresh. Driver or panel-power failures invalidate the baseline.

The current DisplayService contract is single-task. The application must call
all display and batch methods from one task. Do not call `BeginBatch()`, a
refresh method, or `EndBatch()` concurrently. A future multi-task consumer must
first add service-level transaction ownership; the driver mutex alone is not a
service transaction lock.

## R1.2 adaptive full refresh policy

`Auto` and `Fast` use the same policy. Once a valid 1bpp image exists, a changed
submission uses the full OTP path if any of these conditions hold:

| Condition | Threshold on the 400 x 300 panel |
| --- | --- |
| Completed partial refreshes since the last full refresh | 8 |
| Black/white transitions in the pending submission | At least 30,000 pixels (25%) |
| Transitions from successful partial refreshes plus the pending submission | At least 60,000 pixels (50%) |

The driver counts actual changed bits in both directions. The policy does not
use bounding-box area as a substitute: two distant changed pixels still count
as two. Repeated flips of the same pixel each contribute to the accumulated
count, including changes that restore an earlier image. For example, updates
that each flip 12,000 pixels perform four partial refreshes, then a full
refresh for the fifth update. Small clock updates can use the full eight-frame
allowance. Unchanged submissions consume neither budget and do not trigger a
scheduled cleanup.

Only successful partial operations add to the counters. A successful full
refresh resets both counters and the dirty-region union. A refresh or power
failure clears the counters and invalidates the baseline; the next 1bpp
submission must recover with a full frame. Grayscale also invalidates the
1bpp baseline. Explicit packed-patch calls use their supplied full frame for
adaptive cleanup, just as they do for the frame-count limit and recovery.

`epd-inspect` exposes `partial_count`, `partial_pixels` and
`high_contrast_pixels` for observation. The pixel thresholds are initial policy
defaults; Host tests verify selection and state recovery. Physical ghosting,
temperature sensitivity, latency and energy still need panel measurement.
The policy uses the existing OTP full-refresh operation and does not change
waveforms, BUSY handling or power sequencing.

## Other platform services

`InputService` attaches to initialized board support with a typed reference. It
converts debounced physical button results to `InputEvent`. Applications use
logical `Button` and `Action` values. Wait values use native FreeRTOS ticks so
`portMAX_DELAY` keeps its wait-forever meaning. Physical sampling, GPIO
numbers, FreeRTOS queues, and debounce thresholds remain inside board support.

`PowerService` attaches to board support with a typed reference. It exposes
logical battery, external-power, and wake-reason values. It owns the shutdown
rail sequence and the deep-sleep call. The application owner enters shutdown
through `Platform::Shutdown()` after its final display update. Platform first
releases service consumers; PowerService then releases board peripherals
before cutting rails. Board GPIO and rail control remain inside board support.
See [`M2_PLATFORM_COMPOSITION.md`](M2_PLATFORM_COMPOSITION.md) for the cleanup
order and Host coverage.

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
L1.2 adds a separate Storage-owned, on-demand SPIFFS book mount with bounded
listing and exact file reads. It does not format on mount failure. Readers close
their file before Platform releases Storage. See [READER.md](READER.md).
L1.3 adds an exclusive management lease for streamed upload, download and
deletion. Reader handles exclude that lease. Uploads sync a staging file before
renaming it into the library and never replace an existing filename. Connectivity
joins HTTP handlers before releasing the lease. See [BOOK_TRANSFER.md](BOOK_TRANSFER.md).

`SystemService` owns firmware identity, reset reason, chip capabilities, memory
diagnostics, flash size, and the hardware MAC address. Device Info and
diagnostic code get these values from the service. Board support continues to
own the detection of board capabilities such as RTC and NFC. The behavior
baseline is in [`M2_SYSTEM_BASELINE.md`](M2_SYSTEM_BASELINE.md).
