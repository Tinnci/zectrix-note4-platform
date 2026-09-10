# Note4 Open Platform

English | [中文](README_zh.md)

An independent, modular firmware framework and application platform for
ESP32-S3 e-paper devices (targeting the Note4 hardware layout), developed and
maintained by [Tinnci](https://github.com/Tinnci).

It keeps the original board and SSD2683 display support as a qualified hardware
baseline, then adds owned system services, a static application runtime, an
e-reader engine, a source-stable SDK, companion connectivity and bounded
maintenance interfaces.

The goal is to let applications use display, input, power, time, storage and
connectivity capabilities without directly controlling GPIO, SPI, raw
partitions, ESP-NimBLE or FreeRTOS objects. The project remains self-contained
and does not link to the commercial NOTE4 firmware or require LVGL. It is not
the complete NOTE4 consumer firmware or a cloud service.

---

## Notice and Trademark Disclaimer

> [!NOTE]
> **NOTE4** and **ZECTRIX** are product names and trademarks of Zectrix Lab /
> their respective owners. This repository is an independent, community-driven
> open-source software project. It is **not** an official firmware release, and
> it is **not** affiliated with, sponsored by, or endorsed by Zectrix Lab.
>
> Flashing custom firmware replaces the software on your connected device.
> Confirm your hardware revision and serial port before flashing.

## From reference demo to platform

The repository started from
[`itopinion/zectrix-note4-epd-demo`](https://github.com/itopinion/zectrix-note4-epd-demo)
at commit `ca285c98`. See [UPSTREAM.md](UPSTREAM.md) for provenance.

| Preserved upstream baseline | Added in this repository |
| --- | --- |
| SSD2683 1 bpp, partial and 4 bpp display paths | Reproducible ESP-IDF toolchain, build provenance, hardware qualification and factory-recovery procedure |
| NOTE4 board adapters and peripheral access | Display, input, power, time, storage and system service ownership |
| Gallery UI and hardware capability demo | Static application runtime with Launcher, Reader, Settings, Diagnostics and Clock |
| Wi-Fi RF, audio, RTC, charging, LED, buttons, NFC and battery self-tests | Source-stable C++17 SDK v1 with compatibility and architecture checks |
| Basic on-device navigation and shutdown | Versioned companion protocol, durable synchronization, secure BLE transport, Android companion and NFC-assisted enrollment |
| Hardware-oriented serial diagnostics | Bounded maintenance CLI, platform diagnostics and interactive host simulator |

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
| D1 | In progress | USB sessions, platform diagnostics, log streaming and host simulator implemented; input observation and hardware qualification remain open |
| M5 | In progress | A/B partition validation, streamed firmware verification and boot confirmation watchdog implemented; update delivery and hardware qualification remain open |
| R1 | In progress | Minimal dirty-region updates, unchanged-frame suppression and adaptive full-refresh policy implemented; hardware qualification remains open |
| L1 | Implemented | Status bar, scene navigation, streamed TXT/EPUB reader, local Wi-Fi book management and ambient sleep covers; physical sleep/wake and standby-current measurements remain open |
| S1 | Implemented | Typed service registry, selectable modules, conditional application composition, RTC restoration/editor and Full/Minimal regression; physical RTC retention and standby-current measurements remain open |

The [service registry](docs/SERVICE_REGISTRY.md) provides optional typed lookup,
ordered startup and failure cleanup with 16 fixed slots and no registry heap
allocation. Existing Platform accessors use the same service instances.
The [module build options](docs/MODULAR_BUILD.md) select connectivity, Wi-Fi,
HTTPS, Web transfer, reading, USB maintenance and firmware writing. Core boot
protection and the clock/sleep UI remain available in trimmed builds.
Run `bash tools/test-minimal-profile.sh` for the complete Host suite and
isolated Full/Minimal firmware builds with size and static RAM comparison.
Add `--device` to smoke-test both profiles on a connected Note4 and finish on
Full. See the [profile workflow](docs/MODULAR_BUILD.md#repeatable-fullminimal-regression)
for individual build/flash commands and measured results.

The [firmware fork and UI study](docs/FIRMWARE_UI_STUDY.md) compares Biscuit,
CrossMux, CrossInk, Momentum and Unleashed. It proposes daily Home, typography
and personal cover improvements within the existing ownership and power model.
The [Home dashboard](docs/HOME.md) now provides a calendar/reading overview,
Continue Reading, module-aware app tiles and a separate Tools scene.
The [display scheduling update](docs/DISPLAY_RESPONSIVENESS.md) coalesces queued
navigation draws, protects control input under load and bounds driver lock
waits while preserving synchronous display completion and reader bookmarks.

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
- Embedded TRMNL16 UI font and Unifont CJK reader bitmaps at 16px/24px.
- Streamed TXT/EPUB reading from a Storage-owned SPIFFS book partition, with
  NVS bookmarks and durable phone progress synchronization.
- Browser upload, download and deletion over a temporary Wi-Fi hotspot or
  saved home network, with automatic radio shutdown.
- Saved daily dashboard, landscape or blank sleep cover, with calendar and
  reading progress. See [docs/SLEEP_COVER.md](docs/SLEEP_COVER.md).
- Long-press DOWN for 3 seconds to present the sleep cover and shut down
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

The current partition layout preserves factory and NVS locations and adds two
3 MiB OTA slots. Trial firmware must finish startup and its first launcher
render within the boot confirmation window. See
[ADR-0005](docs/adr/0005-ab-ota-boot-confirmation.md) for installation requirements,
rollback behavior, the streamed CRC/header verification API and the
fresh-configuration build command.

## Book reader

Open **BOOK READER**, choose a book with UP/DOWN, and press OK. While reading,
UP/DOWN turn pages and OK opens font and resume options. Hold OK returns to
the library. The current position is saved after each successful page display.
Android shows the synchronized progress and can queue a position for explicit
resume on Note4.

The build creates `build/books.bin` from `books/`. Install it separately with
`idf.py -p PORT books-flash`; this replaces the book partition. Ordinary firmware
flash preserves books. You can select your own source directory through
`-D "ZECTRIX_BOOKS_DIR=/absolute/path/to/books"`. See
[docs/READER.md](docs/READER.md) for installation and format limits.

After the initial library installation, open **SEND BOOKS** to add books over
Wi-Fi. Create a Note4 hotspot or use a saved home network. Open the address
on the device screen and enter its access code. Drag TXT/EPUB files into the
browser and select **Upload & finish**. The same page supports download and
deletion. Wi-Fi turns off when the session ends. See
[docs/BOOK_TRANSFER.md](docs/BOOK_TRANSFER.md) for controls and session limits.

## Host maintenance CLI

Run the maintenance CLI on Linux or macOS with a C++17 compiler. The host
simulator uses the firmware parser, terminal session and diagnostic executor.
Hardware values are synthetic. It does not require ESP-IDF or a connected board.

```bash
bash tools/run-cli-host.sh
# Build once for repeated runs or piped commands.
bash tools/build-cli-host.sh
build-host/zectrix-cli-host --owner-delay-ms 500 --log-burst 80
printf 'sysinfo\nheap\nepd-inspect\n' | build-host/zectrix-cli-host
```

Use `help` to list commands. `Ctrl+C` cancels, `Ctrl+R` reconnects the session,
and `Ctrl+D` exits. `--owner-delay-ms` delays diagnostic replies without stopping
the terminal. `--log-interval-ms 0` disables periodic logs. Piped commands run in
order, and EOF completes pending replies and cancels log streams.

Run terminal and pipe integration tests with `bash tools/test-cli-host.sh`, or
the complete suite with `bash tools/test-host.sh`. Tests require Python 3 and
[uv](https://docs.astral.sh/uv/getting-started/installation/). Python fixtures use
the standard library; module configuration tests provision IDF's Kconfig library
through uv. See the
[maintenance CLI contract](docs/MAINTENANCE_CLI_CONTRACT.md) for the execution
model and limits.

## Controls

| Input | Action |
| --- | --- |
| UP press | Previous item |
| DOWN press | Next item |
| OK click | Select or confirm |
| OK hold (1.5 s) | Return one level or cancel the current action |
| DOWN hold (3 s) | Present sleep cover, power down peripherals and shut down |

When enabled in Settings, the home screen starts Auto Showcase after 15 seconds
of inactivity. New installations leave it off. On
battery power, shutdown releases the hardware power latch. While powered over
USB, the board enters deep sleep after presenting the selected cover. Release
DOWN, then press it again to wake. In **SLEEP COVER**, OK saves a style and opens
its preview; another OK sleeps. Blank clears the whole panel. The dashboard
shows an **AS OF** snapshot and does not update during sleep.

## UI map

```text
Splash
  -> Home
     |-- Reading overview -> Continue Reading / Library
     |-- Book Reader
     |    `-- Library -> Reading -> Font / Phone Position / Restart / Save
     |-- Send Books
     |    `-- Hotspot / Home Network -> Transfer Session
     |-- Clock -> View / Edit
     |-- Sleep Cover
     |    `-- Dashboard / Landscape / Blank -> Preview -> Sleep
     |-- Settings
     `-- Tools
          |-- Connectivity -> Actions / Forget Phone
          |-- Auto Showcase -> 1bpp Full / Partial / 4bpp Full
          |-- Display Gallery -> Lighthouse / Footprints / Mountain / Run All
          |-- Hardware Tests -> Run All / Select Individual
          |-- Device Info
          `-- About & License
```

Hold OK returns one level, including from a tool to its previous Tools row,
then to Home. Hold DOWN sleeps from every first-party page. See
[Unified navigation](docs/NAVIGATION.md) for scene and cleanup behavior.

The 4bpp scene always performs a white 1bpp full refresh first to reduce
ghosting. The display service allows at most eight partial refreshes between
full refreshes. Large black/white changes or accumulated pixel transitions
trigger a full refresh sooner; unchanged frames skip refresh entirely.

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
components/zectrix_reader/    Streaming TXT/EPUB engine, fonts and bookmarks
components/zectrix_*          Owned system services and application runtime
android-companion/            Android BLE/NFC companion under development
protocol/                     Shared protocol golden vectors
main/assets/                  Embedded display assets
books/                        Default content image source
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

Copyright (c) 2026 Zectrix Lab  
Copyright (c) 2026 Tinnci  
Released under the [MIT License](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party notices.
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution instructions.

## Acknowledgments

This project builds upon work from the open-source community:

- **[ZECTRIX Lab](https://wiki.zectrix.com/)** — For developing the original Note4 hardware and open-sourcing the initial hardware demonstration baseline (`itopinion/zectrix-note4-epd-demo`).
- **[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader)** — For design patterns regarding streamed e-paper typography and local web transfer.
- **[Flipper Zero](https://github.com/flipperdevices/flipperzero-firmware)** — For inspiration on bounded scene management, viewport scheduling and CDC-ACM terminal sessions.
- **Heavyweight Type Foundry & GNU Unifont** — For TRMNL16 and Unifont fonts provided under the SIL Open Font License.

## Official links

- [ZECTRIX NOTE4 product page](https://www.zectrix.com/en/note4.html)
- [ZECTRIX Developer Wiki](https://wiki.zectrix.com/)
