# U0 — Upstream reproduction baseline

This manifest freezes the reproducible software baseline for the first
hardware qualification gate. U0 is a reference point. It does not promote
ESP-IDF 5.5.2 to the permanent supported SDK.

## Identity

| Field | Value |
| --- | --- |
| Manifest generated | 2026-08-11T13:47:59+08:00 |
| Project | `zectrix_epd_demo` |
| Target | `esp32s3` |
| Platform commit | `1ca21dd40319928e09fd62a6307f24c2ffe0d6fb` |
| Upstream source commit | `ca285c98ed0641f86780edb1f5ec77b0335fe649` (`v1.0.0`) |
| ESP-IDF tag | `v5.5.2` |
| ESP-IDF superproject commit | `30aaf64524299d3bde422ca9a2848090d1bc5d0f` |
| Component | `espressif/esp_codec_dev` `1.5.11` |
| Target lock | `esp32s3` |
| `dependencies.lock` SHA-256 | `a282ade69000087d5908f1a53ad4a27e29a10b5b7f04df6bfff75b20e61b2118` |

The ESP-IDF version command reports `ESP-IDF v5.5.2-dirty` because the
superproject intentionally materializes only the controlled submodule set.
The superproject commit and every required submodule listed below use pinned
commits. The dirty suffix does not indicate a functional-source change.

## Controlled submodules

| Path | Commit |
| --- | --- |
| `components/esp_wifi/lib` | `01d52d9e69032c486015dc28b08c3bf6aaf348a9` |
| `components/esp_phy/lib` | `3d57415af6e4c92eff2c4c3463e20a51d7340aba` |
| `components/lwip/lwip` | `fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e` |
| `components/mbedtls/mbedtls` | `ffb280bb63c78bfec1e1ab55040671768c85c923` |
| `components/esp_coex/lib` | `63e292b57b2cda9f9496a71a04bec43e1f0caeba` |
| `components/heap/tlsf` | `2867f6883a12920b1969ff9624c0ab0e4185c2ce` |
| `components/bt/controller/lib_esp32c3_family` | `9b50531537e755792ac827d00d233eab499a0b37` |
| `components/bt/host/nimble/nimble` | `9551ac31af0348d7de6cbe7527de3e5ba205460d` |
| `components/unity/unity` | `bf560290f6020737eafaa8b5cbd2177c3956c03f` |
| `components/cmock/CMock` | `eeecc49ce8af123cf8ad40efdb9673e37b56230f` |
| `components/spiffs/spiffs` | `0dbb3f71c5f6fae3747a9d935372773762baf852` |
| `components/protobuf-c/protobuf-c` | `abc67a11c6db271bedbb9f58be85d6f4e2ea8389` |
| `components/json/cJSON` | `c859b25da02955fef659d658b8f324b5cde87be3` |
| `components/bootloader/subproject/components/micro-ecc/micro-ecc` | `24c60e243580c7868f4334a1ba3123481fe1aa48` |

## Toolchain

| Tool | Value |
| --- | --- |
| Xtensa compiler | GCC 14.2.0 (`esp-14.2.0_20251107`) |
| CMake | 3.30.5 |
| Ninja | 1.12.1 |
| IDF Python environment | Python 3.14.6 |
| esptool | 4.12.0 |
| Build mode | `IDF_SKIP_CHECK_SUBMODULES=1` after project admission test |

The admission test is `tools/check-dev-env.sh`. Run
`source tools/activate-dev-env.sh` before the admission test or a build.

## Build artifacts

The clean build completed `1121/1121` tasks on 2026-08-11.

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `bootloader/bootloader.bin` | 21056 | `67fa6c8d0863cbbea1c6f25dc5f1ea161440171d9f01701d38159f64c354cfb6` |
| `partition_table/partition-table.bin` | 3072 | `73c0b5c3e5fcba3a151cc70c453c93dd5f4798899e7f2f8cca76da1f32ffc501` |
| `zectrix_epd_demo.bin` | 1009360 | `f97bb37d5db6aa3408d6b0677d0cb1a7dc437fafdcbd8f4094b9c3996bf7b62b` |
| `zectrix_epd_demo.elf` | 11833756 | `835a65b61786e55c5ff03e4ef96900ce3934c497dc54e8ed9b1c7671d46bbbb1` |

## Safety and gate status

- No flash, erase, eFuse, secure-boot or flash-encryption operation was run.
- Factory backup verification remains the prerequisite for first flash. The
  verified full-image SHA-256 is
  `47a3eb7b97a5a20dd0f3b0904b02ea792504c769941216af7dd4c02498b439d2`.
- This manifest does not include hardware runtime qualification, serial boot-log
  capture or a recovery drill.
