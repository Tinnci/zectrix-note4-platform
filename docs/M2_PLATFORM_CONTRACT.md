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

`Platform` is the composition root for these six services. Boot protection runs
first, then board support and the dependent services in a documented order.
Application code gets non-owning service references from `Platform`. It does
not call a service factory, call `Attach()`, or delete a service.

These services plus BootGuard, Diagnostics and selected Update, Connectivity
and Maintenance providers use a fixed 16-slot `ServiceRegistry`. S1.2 makes the
optional components selectable through [Kconfig](MODULAR_BUILD.md), while
boot protection remains mandatory. Providers implement the
pure virtual `Init()`, `Start()`, `Stop()` lifecycle through embedded adapters.
The composition owner starts providers in dependency order and withdraws each
interface before stopping it. See [SERVICE_REGISTRY.md](SERVICE_REGISTRY.md).

`Platform` stops consumers before their dependencies in reverse registration
order, retaining Input/Power handles until final board cleanup. A failed
initialization cleans up every attempted provider, including partial entry.
The same `Platform` object does not retry initialization after a failure because board
support can be partially initialized. If allocation fails before board
initialization starts, the same object can retry safely.

`Platform::Initialize()` reports allocation failure as `ESP_ERR_NO_MEM` and
cleans up completed service creation. An application must get service
references only after `Initialize()` returns `ESP_OK`. Access before successful
initialization through a reference accessor is a contract violation and
triggers an assertion. `Platform::Services().Get<Interface>()` is the optional
path: it returns `nullptr` for absent, not-yet-ready or stopped providers.
The registry view permits lookup only; Platform keeps lifecycle ownership.

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
- R1.4 selects cleanup from projected spatial debt, temperature and supply observations.
- Large single-frame changes retain the 30,000-pixel full-refresh rule.
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
also applies when accumulated debt is near its cleanup budget. An unknown
service state or invalid driver shadow requires recovery with the supplied full frame, even
when the pixels appear unchanged. `Quality` and `FullClean` always use the
full 1bpp path. A 4bpp frame accepts `Quality` only. The service owns panel power
for each refresh. Driver or panel-power failures invalidate the baseline.

The current DisplayService contract is single-task. The application must call
all display and batch methods from one task. Do not call `BeginBatch()`, a
refresh method, or `EndBatch()` concurrently. A future multi-task consumer must
first add service-level transaction ownership; the driver mutex alone is not a
service transaction lock.

## R1.4 adaptive full refresh policy

R1.4 supersedes R1.2's eight-frame and 60,000-accumulated-pixel thresholds.
`Auto` and `Fast` predict a proposed partial update using twenty 80 x 75 tiles,
actual directional flips, driven-window exposure, local concentration and
signed transition memory. Freshness-aware temperature and battery gains adjust
nonnegative Q16.16 debt. A projected mean of 0.75 units or local peak of 4 units
selects the existing full OTP path. At least 30,000 transitions in one pending
submission still requires full cleanup. Explicit Quality/FullClean, unknown
baseline recovery and grayscale behavior retain their existing paths.

Only successful physical completion, including owned power cleanup, commits
the prediction. Failed frames retain old debt but invalidate the image
baseline; full recovery resets it. Diagnostic frame/pixel totals saturate at
UINT32_MAX and never schedule a refresh. Identical submissions neither refresh
nor relax debt. Default debt relaxation is disabled, so idle time alone cannot
justify forgetting a ghost. Packed patches retain their supplied full fallback.

The parameter dictionary, mathematical assumptions, sampling limits and export
format are described in [DISPLAY_PHYSICS.md](DISPLAY_PHYSICS.md). Sixteen bounded
frame records reuse the driver's actual SPI/BUSY operations and the shell's
existing battery samples. The recorder and CLI copies remain foreground-owned.
No waveform change, extra framebuffer, sensor polling task or periodic wake is
introduced. Model scores are uncalibrated optical-risk proxies; energy estimates
require caller-supplied calibration. `display status`, `display telemetry` and
`display model` inspect this state without triggering hardware work.

## Other platform services

`InputService` attaches to initialized board support with a typed reference. It
converts debounced physical button results to `InputEvent`. Applications use
logical `Button` and `Action` values. Wait values use native FreeRTOS ticks so
`portMAX_DELAY` keeps its wait-forever meaning. Physical sampling, GPIO
numbers, FreeRTOS queues, and debounce thresholds remain inside board support.

E1.3 retains independent button sampling while display calls wait for BUSY.
A fixed 16-event buffer uses a short critical section; its one-slot wake queue
is only a notification and never holds physical input. Enqueue never waits for
capacity. On overflow, confirmation/Back can displace the newest queued
direction; Back can also displace a click when no direction remains. If all
slots are controls, further confirmations are dropped; existing Back events
remain. Shutdown supersedes queued input and is delivered first. Retained
events otherwise keep their order. Maintenance wake notifications do not
consume these slots or extend a wait deadline. The wait hook remains outside
display operations. See [DISPLAY_RESPONSIVENESS.md](DISPLAY_RESPONSIVENESS.md).

`PowerService` attaches to board support with a typed reference. It exposes
logical battery, external-power, and wake-reason values. It owns the shutdown
rail sequence and the deep-sleep call. The application owner enters shutdown
through `Platform::Shutdown()` after its final display update. Platform first
releases service consumers; PowerService then releases board peripherals
before cutting rails. Board GPIO and rail control remain inside board support.
L1.4 presents the selected ambient cover, or a white fallback on failure,
before this release sequence. The final surface remains static through service
destruction. Board support prepares released-DOWN GPIO18 EXT1 wake after
peripheral cleanup, with a bounded release wait; a wake setup failure does not
skip rail-off. No timer wake or periodic redraw is added.
See [`M2_PLATFORM_COMPOSITION.md`](M2_PLATFORM_COMPOSITION.md) for the cleanup
order and Host coverage.

`TimeService` attaches to board support with a typed reference. It owns the
application monotonic-clock boundary, RTC calendar operations, and RTC
countdown status. Board support owns the RTC implementation, I2C operations,
and interrupt GPIO. S1.3 initializes Time after Storage and before Connectivity,
restores UTC from the retained RTC and saved offset, and owns local/Companion
calibration with bounded foreground retries. RTC faults remain nonfatal.
The historical M2 behavior is in [`M2_TIME_BASELINE.md`](M2_TIME_BASELINE.md);
current time and persistence behavior is in [TIME.md](TIME.md).

`StorageService` owns default NVS initialization, recovery policy, and the
platform key-value namespace. Application and self-test code do not call NVS
directly. A successful write or erase commits immediately. D1.5 returns NVS
initialization/open errors without erasing records, including no-free-pages
and newer-format errors. Platform records the error and continues with volatile
defaults, independent book/app files and local maintenance; Connectivity stays
stopped. Persistence errors remain visible. Only explicit factory reset erases
the default NVS partition. The historical behavior baseline is in
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

D1.5 adds a fixed-size, foreground-owned `HealthSupervisor`. Known-good boots
hand the RTC watchdog to a 90-second runtime deadline before board startup;
trial boots retain their 60-second confirmation deadline until a successful
Home frame and `Platform::ConfirmBoot()`. Only completed foreground work or a
completed bounded diagnostic item feeds runtime protection. Polling maintenance
or waiting for input never feeds it. Existing task/interrupt watchdogs remain
unchanged. Protection covers application/service/board teardown and is disarmed
at the final power transition. See [RELIABILITY.md](RELIABILITY.md) for recovery
behavior, observation and fault-injection coverage.
