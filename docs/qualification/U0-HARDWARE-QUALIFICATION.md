# U0 hardware qualification

Status: PREPARED. FLASH NOT AUTHORIZED.

Qualification date: 2026-08-11

## Purpose

Use this record to qualify the U0 firmware on one ZECTRIX NOTE4 device. This
gate verifies the boot chain and the existing hardware paths. It does not
qualify display image quality or a new waveform.

## U0 build identity

| Field | Value |
| --- | --- |
| Platform commit | `1eb64ae94bd225546807da70d45ace8dde04735d` |
| Application version | `v1.0.0-4-g1eb64ae` |
| Target | `esp32s3` |
| ESP-IDF | `v5.5.2` |
| ESP-IDF commit | `30aaf64524299d3bde422ca9a2848090d1bc5d0f` |
| Component lock | `espressif/esp_codec_dev` `1.5.11` |
| Clean build | PASS, `1121/1121` tasks |

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `bootloader/bootloader.bin` | 21056 | `7af0c77e7a1de2534b5136f52ed30489c4096fb01ce03c9c733d1a1225e41e34` |
| `partition_table/partition-table.bin` | 3072 | `73c0b5c3e5fcba3a151cc70c453c93dd5f4798899e7f2f8cca76da1f32ffc501` |
| `zectrix_epd_demo.bin` | 1009360 | `b7cb0897d206ea98a0b3c254735d5d8bcfbbaa5c717b4b052c767b9e6f48bc67` |
| `zectrix_epd_demo.elf` | 11833756 | `2b065a903e5f73a7ec1ab632e99c2dc7e869fe7f873a31336cd4ddbbd5d4f1f8` |

The generated provenance record is `build/build-provenance.txt`. The build
directory is a local derived artifact. Do not commit it.

## Preflight record

| Check | Result |
| --- | --- |
| Factory backup verification | PASS |
| Factory image SHA-256 | `47a3eb7b97a5a20dd0f3b0904b02ea792504c769941216af7dd4c02498b439d2` |
| Serial device | `/dev/ttyACM1` |
| USB identity | Espressif USB Serial/JTAG, private identity matched |
| Chip | ESP32-S3 revision 0.2 |
| Flash | 16 MiB |
| PSRAM | 8 MiB |
| ModemManager | Not active |
| Development environment | PASS, 42 checks and 1 expected submodule warning |

`/dev/ttyACM0` is a FIBOCOM modem. Do not use it for this qualification.

## Authorization boundary

Do not start the flash operation without explicit authorization for this
device and this U0 build. The authorized operation writes the normal ESP-IDF
project images only.

Do not run any of these operations:

- `erase-flash` or `erase_flash`.
- an eFuse write.
- secure-boot enablement.
- flash-encryption enablement.
- a partition redesign.
- a waveform modification.

Stop before writing if the backup check fails, the serial identity changes or
the build identity differs from this record.

## First-flash execution

After explicit authorization, activate the controlled environment. Then run:

```bash
source tools/activate-dev-env.sh
IDF_SKIP_CHECK_SUBMODULES=1 idf.py -p /dev/ttyACM1 flash monitor
```

Capture the complete serial output. Confirm this boot sequence:

1. The bootloader starts.
2. The partition table loads.
3. The application starts.
4. PSRAM initializes.
5. The peripheral initialization completes.
6. The splash screen and UI appear.
7. No panic, watchdog reset or reset loop occurs.

## Qualification results

| Check | Result | Evidence or notes |
| --- | --- | --- |
| Bootloader | PENDING | |
| Partition table | PENDING | |
| Application version in serial log | PENDING | |
| PSRAM | PENDING | |
| Full 1bpp refresh | PENDING | |
| Partial 1bpp refresh | PENDING | |
| Full 4bpp refresh | PENDING | |
| Wi-Fi | PENDING | |
| Audio | PENDING | |
| RTC | PENDING | |
| Power and charging | PENDING | |
| LED | PENDING | |
| Buttons | PENDING | |
| NFC | PENDING | |
| Panic count | PENDING | |
| Watchdog count | PENDING | |
| Unexpected reset count | PENDING | |

Do not block this gate because of subjective 4bpp image quality. Record image
quality observations for the EPD baseline measurement work.

## Exit rule

Keep platform issue #1 open until the serial log proves the application build
identity. Keep platform issue #2 open until all hardware checks and the factory
recovery drill pass.
