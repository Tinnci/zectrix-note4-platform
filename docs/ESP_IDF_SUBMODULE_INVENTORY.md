# ESP-IDF 5.5.2 Submodule Inventory

Inventory date: 2026-08-11

Policy scope: ESP32-S3, the current baseline, and explicitly planned platform
capabilities. A complete recursive checkout is not a project prerequisite.

| Path | Expected use | Current status | Disk usage (worktree + Git objects) | Action |
| --- | --- | --- | ---: | --- |
| `components/esp_wifi/lib` | Baseline Wi-Fi | Complete at `01d52d9e` | 29M + 91M | KEEP |
| `components/esp_phy/lib` | Wi-Fi/BLE PHY | Complete at `3d57415a` | 9.0M + 13M | KEEP |
| `components/lwip/lwip` | Baseline networking | Complete at `fd432e4e` | 9.1M + 15M | KEEP |
| `components/mbedtls/mbedtls` | TLS, HTTPS and OTA | Complete at `ffb280bb` | 50M + 6.7M | KEEP |
| `components/esp_coex/lib` | Planned Wi-Fi/BLE coexistence | Complete at `63e292b5` | 932K + 2.1M | KEEP |
| `components/heap/tlsf` | IDF memory management | Complete at `2867f688` | 76K + 188K | KEEP |
| `components/bt/controller/lib_esp32c3_family` | ESP32-S3 BLE controller | Complete at `9b505315` | 5.8M + 17M | KEEP |
| `components/bt/host/nimble/nimble` | Planned BLE host | Complete at `9551ac31` | 12M + 22M | KEEP |
| `components/bt/esp_ble_mesh/lib/lib` | No current BLE Mesh plan | Partial, mismatched HEAD | 4K + 15M | DEFER |
| `components/bt/controller/lib_esp32` | Non-S3 controller | Complete but unrelated | 864K + 9.1M | CANDIDATE_FOR_REMOVAL |
| `components/bt/controller/lib_esp32c2/esp32c2-bt-lib` | Non-S3 controller | Partial, mismatched HEAD | 4K + 19M | CANDIDATE_FOR_REMOVAL |
| `components/bt/controller/lib_esp32c5/esp32c5-bt-lib` | Non-S3 controller | Partial, mismatched HEAD | 4K + 18M | CANDIDATE_FOR_REMOVAL |
| `components/bt/controller/lib_esp32c6/esp32c6-bt-lib` | Non-S3 controller | Partial, mismatched HEAD | 4K + 48M | CANDIDATE_FOR_REMOVAL |
| `components/bt/controller/lib_esp32h2/esp32h2-bt-lib` | Non-S3 controller | Partial, mismatched HEAD | 4K + 34M | CANDIDATE_FOR_REMOVAL |
| `components/openthread/openthread` | Outside roadmap | Partial, mismatched HEAD | 4K + 130M | CANDIDATE_FOR_REMOVAL |
| `components/openthread/lib` | Outside roadmap | Not initialized | 0 | CANDIDATE_FOR_REMOVAL |
| `components/mqtt/esp-mqtt` | On demand | Complete at `6af4446a` | 472K + 3.1M | DEFER |
| `components/unity/unity` | IDF configure-time dependency | Complete at `bf560290` | 1.6M + 696K | COMPLETE |
| `components/cmock/CMock` | IDF configure-time dependency | Complete at `eeecc49c` | 3.7M + 4.8M | COMPLETE |
| `components/spiffs/spiffs` | IDF configure-time dependency | Complete at `0dbb3f71` | 576K + 1.3M | COMPLETE |
| `components/protobuf-c/protobuf-c` | IDF generate-time dependency | Complete at `abc67a11` | 912K + 1.7M | COMPLETE |
| `components/json/cJSON` | IDF generate-time dependency | Complete at `c859b25d` | 1.9M + 2.8M | COMPLETE |
| `components/bootloader/subproject/components/micro-ecc/micro-ecc` | Bootloader build dependency | Complete at `24c60e24` | 1.4M + 644K | COMPLETE |

The clean non-minimal build exposed Unity, CMock, SPIFFS, protobuf-c, cJSON,
and micro-ecc as concrete configure, generate, or bootloader dependencies.
They were completed individually. No global recursive submodule update was used
after this policy was adopted.

NFC remains implemented by the board component over I2C and GPIO. It does not
justify OpenThread or another radio protocol stack.
