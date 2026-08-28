# ZECTRIX NOTE4 Open Firmware Platform

English | [中文](README_zh.md)

This project turns the upstream ZECTRIX NOTE4 ESP-IDF hardware demo into a
reproducible, low-power application platform. It keeps the original board and
SSD2683 display support as a qualified hardware baseline, then adds owned
system services, a static application runtime, a source-stable SDK, companion
connectivity and bounded maintenance interfaces.

The goal is to let applications use display, input, power, time, storage and
connectivity capabilities without directly controlling GPIO, SPI, raw
partitions, ESP-NimBLE or FreeRTOS objects. The project remains self-contained
and does not link to the commercial NOTE4 firmware or require LVGL. It is not
the complete NOTE4 consumer firmware or a cloud service.

## From reference demo to platform

The repository started from
[`itopinion/zectrix-note4-epd-demo`](https://github.com/itopinion/zectrix-note4-epd-demo)
at commit `ca285c98`. See [UPSTREAM.md](UPSTREAM.md) for provenance.

| Preserved upstream baseline | Added in this repository |
| --- | --- |
| SSD2683 1 bpp, partial and 4 bpp display paths | Reproducible ESP-IDF toolchain, build provenance, hardware qualification and factory-recovery procedure |
| NOTE4 board adapters and peripheral access | Display, input, power, time, storage and system service ownership |
| Gallery UI and hardware capability demo | Static multi-application runtime with Launcher, Settings, Diagnostics and Clock |
| Wi-Fi RF, audio, RTC, charging, LED, buttons, NFC and battery self-tests | Source-stable C++17 SDK v1 with compatibility and architecture checks |
| Basic on-device navigation and shutdown | Versioned companion protocol, durable synchronization, secure BLE transport, Android companion and NFC-assisted enrollment |
| Hardware-oriented serial diagnostics | Bounded maintenance CLI architecture and tested parser core |

## Development status

Development follows dependency-aware stage gates defined in
[docs/ROADMAP.md](docs/ROADMAP.md).

| Stage | Status | Result |
| --- | --- | --- |
| M1 | Complete | Reproducible upstream baseline, hardware qualification and factory recovery |
| M2 | Complete | Platform-owned display, input, power, time, storage and system services |
| M3 | Complete | Static application lifecycle and first-party applications |
| M4 | Complete | Source-stable SDK v1 and unified software/hardware exit gate |
| C1 | In progress | Companion protocol, durable sync, secure BLE/Android path and NFC-assisted enrollment; full hardware qualification remains open |
| D1 | In progress | Bounded maintenance CLI core; service commands and hardware exit gate remain open |
| M5 | Planned | Measured A/B update, rollback and recovery architecture |

> [!CAUTION]
> This project targets the black-and-white ZECTRIX NOTE4 hardware. It is not
> compatible with NOTE4C. Flashing replaces the firmware on the connected
> device. Confirm the model and serial port before you flash. If you flash the
> wrong device, you can lose its current firmware.

![Footprint animation preview](main/assets/snow_path_footprints_preview.png)

## Hardware baseline

- 400 x 300 SSD2683 e-paper display
- Full-screen 1bpp refresh, partial 1bpp refresh and full-screen 4bpp/16-gray
  refresh
- Display gallery with a lighthouse print, six-step footprint animation and
  high-contrast grayscale landscape
- Wi-Fi RF scan, acoustic speaker/microphone loopback, PCF8563 RTC, charging,
  LED, three-button and NFC self-tests
- Device information page for flash, PSRAM, MAC address, peripherals and power
- Embedded proportional-width TRMNL16 ASCII bitmap font. The demo has no
  runtime font or filesystem dependency.
- Long-press DOWN for 3 seconds to clear the panel and shut down
- MIT licensed by ZECTRIX Lab

## Quick start

Requirements: the project-qualified ESP-IDF v5.5.2 baseline and a ZECTRIX
NOTE4 4.2-inch ESP32-S3 black-and-white e-paper board.

```bash
# Run these commands from the repository root.
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the board's serial port. Exit the monitor with
`Ctrl+]`. The first configure/build downloads the official
`espressif/esp_codec_dev` component.

See [docs/QUICK_START.md](docs/QUICK_START.md) for setup and troubleshooting.

See [docs/PREREQUISITES.md](docs/PREREQUISITES.md) for development
prerequisites. See [docs/TOOLCHAIN_POLICY.md](docs/TOOLCHAIN_POLICY.md) for the
ESP-IDF version policy. See
[docs/CONTROLLED_TECHNICAL_ENGLISH.md](docs/CONTROLLED_TECHNICAL_ENGLISH.md)
for the documentation style policy.

## Controls

| Input | Action |
| --- | --- |
| UP press | Previous item |
| DOWN press | Next item |
| OK click | Select or confirm |
| OK hold (1.5 s) | Return or cancel |
| DOWN hold (3 s) | Clear display, power down peripherals and shut down |

The home screen starts Auto Showcase after 15 seconds of inactivity. On
battery power, shutdown releases the hardware power latch. While powered over
USB, the board enters deep sleep after clearing the display.

## UI map

```text
Splash
  -> Home
     |-- Auto Showcase
     |    `-- 1bpp Full -> 1bpp Partial -> 4bpp Full
     |-- Display Gallery
     |    |-- Lighthouse / Full 1bpp
     |    |-- Footprints / Partial 1bpp
     |    |-- Mountain / Full 4bpp
     |    `-- Run All Scenes
     |-- Hardware Tests
     |    |-- Run All Tests
     |    `-- Select Individual
     |         `-- Wi-Fi RF / Audio / RTC / Power / LED / Buttons / NFC
     |-- Device Info
     `-- About & License
```

The 4bpp scene always performs a white 1bpp full refresh first to reduce
ghosting. The UI performs a full refresh after eight partial refreshes to limit
accumulated artifacts.

## Configuration

Open `idf.py menuconfig`, then select **Zectrix hardware showcase**:

- `Optional Wi-Fi SSID for RF qualification`: leave this value empty for
  generic scan mode. A configured SSID requires three consecutive qualifying
  observations.
- `RF qualification threshold (dBm)`: default `-70`.
- `Temporary NFC demonstration URL`: default `https://www.zectrix.com`.

## Repository layout

```text
components/zectrix_epd/       Public SSD2683 display driver
components/zectrix_board/     Board pins and peripheral adapters
components/zectrix_demo_ui/   Canvas, bitmap font and English UI
components/zectrix_self_test/ Hardware test implementations
components/zectrix_platform/  Platform composition root
components/zectrix_*          Owned system services and application runtime
android-companion/            Android BLE/NFC companion under development
protocol/                     Shared protocol golden vectors
main/assets/                  Embedded display assets
tools/                        Host tests, checks and asset conversion tools
docs/                         Architecture, contracts and qualification records
```

See [docs/EPD_API.md](docs/EPD_API.md) for the public EPD interface. See
[docs/HARDWARE.md](docs/HARDWARE.md) for hardware pins and
[docs/TEST_CRITERIA.md](docs/TEST_CRITERIA.md) for test criteria.

## Image asset formats

- 1bpp: 400 x 300, row-major, MSB first, `0` black and `1` white. Size:
  15,000 bytes.
- 4bpp: 400 x 300, two pixels per byte, left pixel in the high nibble,
  `0` black and `15` white. Size: 60,000 bytes.

Regenerate assets with:

```bash
python tools/prepare_1bpp.py input.png output.png output.bin
python tools/prepare_4bpp.py input.png output.png output.bin
python tools/generate_ascii_font.py --help
```

Run `tools/prepare_footprint_animation.py --help` for footprint animation
generator instructions.

## License

Copyright (c) 2026 Zectrix Lab. Released under the [MIT License](LICENSE).
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party notices.
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution instructions.

## Official links

- [ZECTRIX NOTE4 product page](https://www.zectrix.com/en/note4.html)
- [ZECTRIX Developer Wiki](https://wiki.zectrix.com/)
