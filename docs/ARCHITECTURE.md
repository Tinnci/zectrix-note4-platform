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

- send SSD2683 commands;
- select waveform tables;
- control display power GPIO;
- access raw partitions;
- call `esp_deep_sleep_start()`;
- depend on internal ESP-IDF driver handles.

## Display boundary

The raw EPD component owns synchronous panel transactions. The display service
owns framebuffer state, valid 1 bpp baselines, dirty regions, partial-update
budgets, refresh profiles and ghosting cleanup.

Applications submit display intent such as `AUTO`, `FAST`, `QUALITY` or
`FULL_CLEAN`. Applications do not select a waveform.

## Application boundary

The first application model is a static registry. A dynamic ELF or WebAssembly
runtime is out of scope until the static API has been exercised by at least
three independent applications and the SDK ABI is frozen.

## Update boundary

The partition table is not frozen. A/B OTA, rollback, assets and user-data
requirements must be measured before a replacement layout is accepted.
