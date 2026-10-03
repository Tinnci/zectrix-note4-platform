# Build from source

For device controls and everyday use, read the [English handbook](HANDBOOK.md)
or [中文手册](HANDBOOK_zh.md). This guide covers development builds.

## 1. Prepare the environment

Install the [qualified tools](PREREQUISITES.md) on Linux or macOS:
ESP-IDF 5.5.2 for `esp32s3`, CMake 3.30.5, ccache, uv and Bun 1.4.2.
From the repository root:

```bash
source tools/activate-dev-env.sh
tools/check-dev-env.sh
```

For an existing checkout with old names/configuration, follow the
[SDK v2 migration](NOTE4_MIGRATION.md) first. Do not delete saved configuration
or erase device data just to apply new defaults.

## 2. Build and test

```bash
tools/build-firmware.sh --profile full
tools/test-host.sh --jobs 2
```

The helper sets `esp32s3`, enables ccache and reports size/partition capacity.
Full output is in `build-full/`. Use `--profile reader` for an offline reader
or `--profile minimal` for clock/settings/sleep/diagnostics.
Named profiles preserve the repository-root `sdkconfig`.
[Custom profiles and menuconfig](MODULAR_BUILD.md).

For a targeted test: `tools/test-host.sh --suite ui` or
`tools/test-host.sh --test reader --jobs 1 --verbose`.
Android: `tools/test-android-companion.sh`.
[Coverage and reports](CI.md).

## 3. Flash only after checking the device

This firmware targets **black-and-white Note4, not NOTE4C**.
Confirm the hardware revision, 16 MiB Flash, octal PSRAM and exact serial port.
Back up settings/content before any layout migration. The first A/B layout
installation needs the bootloader and partition table as well as the app;
an application-only update cannot migrate an old layout.

Replace `PORT` with the confirmed device port:

```bash
idf.py -B build-full -p PORT flash monitor
```

Typical ports: Linux `/dev/ttyACM*` / `/dev/ttyUSB*`,
macOS `/dev/cu.usbmodem*`. Exit the monitor with `Ctrl+]`.
Normal firmware flashing preserves books on the matching partition layout.
The separate `books-flash` operation initializes content: do not use it for
an ordinary firmware upgrade. [Library setup](READER.md).

## 4. Use the device

UP/DOWN moves focus; OK selects; hold OK returns. Hold DOWN for about three
seconds to sleep, release it, then press again to wake. USB can keep the rail
powered while the device sleeps. A held button can disable button wake;
release it before testing sleep/wake.

See [Home](HOME.md), [Reader](READER.md), [USB](USB_HOST.md),
[Wi-Fi transfer](BOOK_TRANSFER.md) and [sleep/wake configuration](SLEEP_COVER.md).
Automatic calendar/remote refresh depends on a valid clock, configuration
and wake/radio budgets; a retained image does not imply an active connection.

## Troubleshooting

- Component download: check registry access and `main/idf_component.yml`.
- Reset loop: verify target, Flash/PSRAM settings and installed partition layout.
- Display BUSY timeout: check cable/pin mapping; partial refresh needs a valid
  full mono baseline. [Display ownership](DISPLAY_ARCHITECTURE.md).
- Sleep/current problems: distinguish USB-powered deep sleep from battery
  rail shutdown. Host simulation is not a current measurement.
