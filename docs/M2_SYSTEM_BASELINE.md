# M2 SystemService baseline

Status: Draft. This baseline applies to issue #13 Stage A. It is not an SDK v1
specification.

## Purpose

`SystemService` is the single source for system identity, reset information,
capabilities, and basic diagnostics. Application code does not call the ESP-IDF
system information APIs directly.

## Preserved behavior

Device Info shows these values:

- MCU model
- flash and PSRAM capacity in MiB
- Wi-Fi station MAC address
- RTC and NFC availability

The audio self-test continues to use the Wi-Fi station MAC address as its test
identity input. The migration does not change the acoustic test algorithm.

## Service data

The service reports:

- project, firmware, ESP-IDF, build, and application ELF identity
- normalized reset reason
- chip revision, core count, Wi-Fi, Bluetooth LE, RTC, NFC, and PSRAM capability
- flash capacity and internal heap diagnostics
- Wi-Fi station MAC address

An ESP-IDF read error is returned to the caller. The service does not invent a
value after an error.

## Stage boundary

Stage A established `SystemService` ownership and migrated existing consumers.
Stage B gets Diagnostics from the Platform composition root. Platform supplies
typed Input, Power, Time, Storage, and System service references. The
application does not supply board support or construct Diagnostics.
