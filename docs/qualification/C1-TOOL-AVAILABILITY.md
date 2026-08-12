# C1 tool and hardware availability

Checked: 2026-08-12 (Asia/Shanghai).

## Available

- Zectrix Note4 on `/dev/ttyACM1` as an Espressif USB JTAG/serial device.
- ESP32-S3 ESP-IDF 5.5.2 toolchain at
  `/home/drie/esp/esp-idf-v5.5.2`.
- ESP-NimBLE, ESP32-S3 Bluetooth controller, coexistence, Wi-Fi and TLS
  dependencies at their pinned ESP-IDF commits.
- Host Bluetooth adapter `E0:4F:43:FD:10:5C`, powered through BlueZ, with
  Central and Peripheral roles.
- Android SDK in `/opt/android-sdk`, including platform tools, Android 37.1
  platform and build-tools 37.0.0.
- Java 21, Gradle 9.6.1, Kotlin 2.3.21 and `adb`.

## Not currently available

- `adb devices -l` reports no connected Android target.
- No Android emulator image is installed.
- No identified precision current or power analyzer is connected.

## Gate effect

The host adapter can qualify BLE advertisements, GATT framing and reconnect
independently of Android. The local Android toolchain can build the companion
and run JVM tests. It cannot prove Companion Device Manager, Android process
lifecycle or real phone BLE behavior without an Android target.

Note4 firmware and Wi-Fi tests can run now. Absolute current measurements
cannot be claimed until a suitable instrument is available. C1.5, C1.6, C1.8,
C1.9 and the C1 umbrella stay open until their physical evidence exists.

`/dev/ttyACM0` is a Fibocom modem and is not a Note4 target. Flash procedures
must continue to resolve and verify `/dev/ttyACM1` before every write.
