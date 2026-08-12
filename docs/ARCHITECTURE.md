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

## Update boundary

The partition table is not frozen. Measure A/B OTA, rollback, assets and
user-data requirements before accepting a replacement layout.

## Connectivity boundary

Connectivity uses a BLE-first companion protocol and an on-demand direct
Wi-Fi backend. Applications request durable state, commands or resources. They
do not select a transport and do not receive ESP-NimBLE, GATT, Wi-Fi, socket or
RTOS types. The initial connectivity semantic boundary is internal and draft;
SDK 1.0 remains unchanged. See `docs/adr/0004-connectivity-platform.md` and
`docs/CONNECTIVITY_CONTRACT.md`.
