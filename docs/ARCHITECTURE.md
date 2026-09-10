# Architecture boundaries

## Purpose

The project develops a low-power application platform for the black-and-white
Zectrix Note 4. ESP-IDF and FreeRTOS remain the operating kernel. This project
provides board support, system services, a display policy layer and an
application framework.

## Dependency direction

```text
applications
    |
public platform API
    |
display / input / power / time / storage / network services
    |
board support and raw EPD driver
    |
ESP-IDF and FreeRTOS
```

Applications must not directly:

- send SSD2683 commands.
- select waveform tables.
- control display power GPIO.
- access raw partitions.
- call `esp_deep_sleep_start()`.
- depend on internal ESP-IDF driver handles.

## Display boundary

The raw EPD component owns synchronous panel transactions. The display service
owns framebuffer state, valid 1 bpp baselines, dirty regions, partial-update
budgets, refresh profiles and ghosting cleanup.

Applications submit display intent such as `AUTO`, `FAST`, `QUALITY` or
`FULL_CLEAN`. Applications do not select a waveform.

For automatic 1bpp updates, the driver compares the submitted pixels against
its existing 15,000-byte shadow and computes exact changed bounds. The service
skips unchanged submissions and applies its partial-refresh budget and full
recovery policy. The budget includes both a maximum of eight partial frames
and accumulated black/white transitions. Large contrast changes select the
existing full OTP operation immediately. The driver reports actual changed
pixels and owns byte alignment and old/new pixel encoding.
The demo UI submits the full canvas, which removes its separate 15,000-byte
patch buffer and includes header/footer changes in the comparison.

## Application and kernel boundary

The first application model is a static registry. Do not add a dynamic ELF or
WebAssembly runtime yet. The runtime owns one active foreground application and
uses owned deferred commands. It processes navigation only after the current
callback returns. Private pages stay inside their application.

SDK v1 is the source-stable application-control boundary. It contains no
ESP-IDF, FreeRTOS, board, driver, or Platform implementation type. It promises
source compatibility for statically linked C++17 applications. It does not
promise a binary ABI.

The application registry is an explicit immutable table. The runtime validates
it before launch and creates one inactive candidate at a time. A typed factory
object creates the candidate and receives the read-only registry. Application
code does not use an application-runtime singleton.

IDF FreeRTOS is the kernel and scheduler. It is a mechanism layer below the
Zectrix runtime. Product architecture does not use task topology as its
dependency graph. See `docs/adr/0003-freertos-runtime-sdk-boundary.md`.

[E2.1 research](DYNAMIC_APPLICATION_RESEARCH.md) compares ELF, Wasm3, WAMR and
Lua with executable Host probes and isolated ESP32-S3 link measurements. It
proposes a versioned guest boundary and an Apps adapter within this ownership
model. Dynamic loading remains future implementation; the normal firmware and
SDK v1 do not acquire an engine dependency or a binary ABI promise.

## Service composition

Platform's internal `ServiceRegistry` holds 16 borrowed, typed provider slots.
Each provider implements `Init`, `Start` and `Stop`; Platform starts them in
dependency order and stops them in reverse order. Failed startup unwinds the
attempted providers. Lookup returns `nullptr` for absent or retired interfaces,
while existing mandatory accessors keep their initialization assertions.

The registry and lifecycle adapters use fixed member storage and no additional
heap allocations or tasks. Platform remains the resource owner; applications
receive a read-only lookup view. S1.2 selects optional services, applications
and component dependencies through [Kconfig](MODULAR_BUILD.md). Core boot
protection remains enabled in every profile. Broader application composition
and persistent RTC work continue in S1.3.
See [SERVICE_REGISTRY.md](SERVICE_REGISTRY.md) for lifecycle and lifetime rules.

## Update boundary

The partition table is not frozen. The M5.1 layout retains the factory and NVS
locations and adds two OTA application slots and native OTA metadata.
Core `BootGuard` owns boot validation and trial-boot confirmation. The optional
`UpdateService` shares that guard, validates the inactive destination and
receives firmware chunks. It validates chunk and image CRC-32 values,
the image header, native image bounds and flash readback before selecting the
next boot slot. A retained RTC watchdog resets an unconfirmed boot even if the
application cannot make progress. Update delivery remains a separate consumer
of the service. Measurements, compatibility and recovery limits are recorded
in [ADR-0005](adr/0005-ab-ota-boot-confirmation.md).

## Connectivity boundary

Connectivity uses a BLE-first companion protocol and an on-demand direct
Wi-Fi backend. Applications request durable state, commands or resources. They
do not select a transport and do not receive ESP-NimBLE, GATT, Wi-Fi, socket or
RTOS types. The initial connectivity semantic boundary is internal and draft;
SDK 1.0 remains unchanged. See `docs/adr/0004-connectivity-platform.md` and
`docs/CONNECTIVITY_CONTRACT.md`.
