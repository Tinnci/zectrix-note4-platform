# ZECTRIX NOTE4 E-Paper Reference Demo

> This repository is the public development base for the Zectrix Note 4 open
> firmware platform. It preserves the upstream reference demo as the initial
> hardware baseline. See [docs/ROADMAP.md](docs/ROADMAP.md) before proposing
> platform or application changes.

English | [中文](README_zh.md)

A standalone, open-source ESP-IDF reference project for the ZECTRIX NOTE4
4.2-inch black-and-white e-paper hardware. It demonstrates the display, audio,
Wi-Fi, RTC, charging, LED, buttons, NFC and battery-monitoring capabilities
through an English on-device UI.

This project is self-contained under this directory. It does not link to the
commercial NOTE4 firmware or require LVGL. It is a hardware demonstration and
driver reference, not the complete NOTE4 consumer firmware or cloud service.

> [!IMPORTANT]
> This project targets the black-and-white ZECTRIX NOTE4 hardware. It is not
> compatible with NOTE4C. Flashing this demo replaces the firmware currently
> installed on the connected device, so confirm the model and serial port
> before running any flash command.

![Footprint animation preview](main/assets/snow_path_footprints_preview.png)

## Highlights

- 400 x 300 SSD2683 e-paper display
- Full-screen 1bpp refresh, partial 1bpp refresh and full-screen 4bpp/16-gray
  refresh
- Display gallery with a lighthouse print, six-step footprint animation and
  high-contrast grayscale landscape
- Wi-Fi RF scan, acoustic speaker/microphone loopback, PCF8563 RTC, charging,
  LED, three-button and NFC self-tests
- Device information page for flash, PSRAM, MAC address, peripherals and power
- Embedded proportional-width TRMNL16 ASCII bitmap font; no runtime font or
  filesystem dependency
- Long-press DOWN for 3 seconds to clear the panel and shut down
- MIT licensed by ZECTRIX Lab

## Quick start

Requirements: ESP-IDF 5.4 or later and a ZECTRIX NOTE4 4.2-inch ESP32-S3
black-and-white e-paper board.

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
ghosting. UI partial refreshes are periodically replaced by a full refresh to
limit accumulated artifacts.

## Configuration

Open `idf.py menuconfig`, then select **Zectrix hardware showcase**:

- `Optional Wi-Fi SSID for RF qualification`: empty means generic scan mode;
  a configured SSID requires three consecutive qualifying observations.
- `RF qualification threshold (dBm)`: default `-70`.
- `Temporary NFC demonstration URL`: default `https://www.zectrix.com`.

## Repository layout

```text
components/zectrix_epd/       Public SSD2683 display driver
components/zectrix_board/     Board pins and peripheral adapters
components/zectrix_demo_ui/   Canvas, bitmap font and English UI
components/zectrix_self_test/ Hardware test implementations
main/assets/                  Embedded display assets
tools/                        Asset and font conversion utilities
docs/                         Integration and validation guides
```

The public EPD interface is documented in
[docs/EPD_API.md](docs/EPD_API.md). Hardware pins and tests are documented in
[docs/HARDWARE.md](docs/HARDWARE.md) and
[docs/TEST_CRITERIA.md](docs/TEST_CRITERIA.md).

## Image asset formats

- 1bpp: 400 x 300, row-major, MSB first, `0` black and `1` white; 15,000 bytes.
- 4bpp: 400 x 300, two pixels per byte, left pixel in the high nibble,
  `0` black and `15` white; 60,000 bytes.

Regenerate assets with:

```bash
python tools/prepare_1bpp.py input.png output.png output.bin
python tools/prepare_4bpp.py input.png output.png output.bin
python tools/generate_ascii_font.py --help
```

The footprint animation generator is documented in
`tools/prepare_footprint_animation.py --help`.

## License

Copyright (c) 2026 Zectrix Lab. Released under the [MIT License](LICENSE).
Third-party notices are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Contributions are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Official links

- [ZECTRIX NOTE4 product page](https://www.zectrix.com/en/note4.html)
- [ZECTRIX Developer Wiki](https://wiki.zectrix.com/)
