# M2 storage behavior baseline

Status: Draft.

This document records the persistent-state behavior before the M2.5 ownership
migration. The migration changes the dependency boundary. It does not add a
filesystem or change the qualified Wi-Fi self-test behavior.

## Existing behavior

- The Wi-Fi self-test initializes the default NVS partition when the test
  starts.
- If initialization reports no free pages or a newer unsupported NVS format,
  the self-test erases the default NVS partition and retries initialization.
- Other initialization errors stop the Wi-Fi self-test.
- Application code does not currently store settings or configuration.

## StorageService policy

- `StorageService` owns default NVS initialization and recovery policy.
- Initialization remains on demand. The application does not initialize or
  erase NVS at boot only because the service exists.
- Platform-owned values use the `zectrix` NVS namespace.
- A successful write or erase operation commits immediately.
- Read operations do not modify storage.
- The service supports Boolean, signed 32-bit, unsigned 32-bit, string, and
  small binary values.
- The service returns the underlying ESP-IDF error result. It does not replace
  a missing value with an implicit default.

## Excluded scope

- M2.5 does not select SPIFFS, LittleFS, or another filesystem.
- M2.5 does not define asset storage, an application package format, or an
  application database.
- M2.5 does not define encryption or a schema-migration framework.
