# U0 hardware qualification

Status: U0 HARDWARE QUALIFICATION AND FACTORY RECOVERY PASS.

Qualification date: 2026-08-11

## Purpose

Use this record to qualify the U0 firmware on one ZECTRIX NOTE4 device. This
gate verifies the boot chain and the existing hardware paths. It does not
qualify display image quality or a new waveform.

## U0 build identity

| Field | Value |
| --- | --- |
| Flashed platform commit | `20bb282d3a6bb7b8acea726bf21be391663118bd` |
| Application version | `v1.0.0-5-g20bb282` |
| Target | `esp32s3` |
| ESP-IDF | `v5.5.2` |
| ESP-IDF commit | `30aaf64524299d3bde422ca9a2848090d1bc5d0f` |
| Component lock | `espressif/esp_codec_dev` `1.5.11` |
| Clean build | PASS, `1121/1121` tasks at functional-source commit `1eb64ae` |
| Flash-time rebuild | PASS, 11 incremental tasks at commit `20bb282` |

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `bootloader/bootloader.bin` | 21056 | `7af0c77e7a1de2534b5136f52ed30489c4096fb01ce03c9c733d1a1225e41e34` |
| `partition_table/partition-table.bin` | 3072 | `73c0b5c3e5fcba3a151cc70c453c93dd5f4798899e7f2f8cca76da1f32ffc501` |
| `zectrix_epd_demo.bin` | 1009360 | `1568d1a081ee0b6dd3615ebc4e4d9c7ed3b410c33346f12734cd8c46e75eda4f` |
| `zectrix_epd_demo.elf` | 11833756 | `36fbfbee919f0cfc5f26ea0b7aa0f53aafdc28e576030ed473861979f87d0f7d` |

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

## Authorization and safety boundary

The operator explicitly authorized the first flash on 2026-08-11. The
authorized operation wrote the normal ESP-IDF project images only.

Do not run any of these operations:

- `erase-flash` or `erase_flash`.
- an eFuse write.
- secure-boot enablement.
- flash-encryption enablement.
- a partition redesign.
- a waveform modification.

For a future write, stop if the backup check fails, the serial identity changes
or the build identity differs from the approved record.

## First-flash execution

The authorized session used this command:

```bash
source tools/activate-dev-env.sh
IDF_SKIP_CHECK_SUBMODULES=1 idf.py -p /dev/ttyACM1 flash monitor
```

Capture the full serial output. Confirm this boot sequence:

1. The bootloader starts.
2. The partition table loads.
3. The application starts.
4. PSRAM initializes.
5. The peripheral initialization completes.
6. The splash screen and UI appear.
7. No panic, watchdog reset or reset loop occurs.

The local session log is `build/u0-first-flash-session.log`. It contains a
private device identity and remains an uncommitted derived artifact.

### Controlled deviation

The preflight artifact came from clean functional-source commit `1eb64ae`.
Commit `20bb282` then added this qualification document. The flash command
detected the new Git commit and rebuilt the application before writing. No
functional source changed. The table above records the hashes that the flash
command actually wrote and verified.

The first monitor connection lost the USB device after application handoff.
The operator disconnected and reconnected USB once. The second monitor session
captured a successful boot. Do not classify this manual USB reset as
an unexpected firmware reset.

## Qualification results

The operator visually confirmed all three display scenes on 2026-08-11.
The operator also confirmed that the final seven-item hardware-test summary
reported PASS for every test. The serial log contains explicit Audio PASS
evidence and the Wi-Fi initialization and scan lifecycle. The current firmware
does not print every final interactive test result to the serial console.

| Check | Result | Evidence or notes |
| --- | --- | --- |
| Bootloader | PASS | ESP-IDF v5.5.2 second-stage bootloader started. |
| Partition table | PASS | NVS, PHY and 3 MiB factory app loaded. |
| Application version in serial log | PASS | `v1.0.0-5-g20bb282` |
| PSRAM | PASS | 8 MiB detected. SPI SRAM memory test passed. |
| Board initialization | PASS | RTC and NFC initialization completed. |
| Full 1bpp refresh | PASS | Scene 0 ran twice. The operator confirmed the output. |
| Partial 1bpp refresh | PASS | Scene 1 ran twice. The operator confirmed the output. |
| Full 4bpp refresh | PASS | Scene 2 ran twice. The operator confirmed the output. |
| Wi-Fi | PASS | Driver and scan lifecycle logged. Operator confirmed PASS summary. |
| Audio | PASS | Serial log reports `result=PASS`. Operator confirmed PASS summary. |
| RTC | PASS | Operator confirmed PASS summary. |
| Power and charging | PASS | Operator confirmed PASS summary. |
| LED | PASS | Operator confirmed visible LED result and PASS summary. |
| Buttons | PASS | Operator completed the specified sequence and confirmed PASS summary. |
| NFC | PASS | NFC initialized. Operator confirmed field test and PASS summary. |
| Panic count | PASS | 0 through the display and hardware-test run. |
| Watchdog count | PASS | 0 through the display and hardware-test run. |
| Unexpected reset count | PASS | 0 after the successful monitored boot. |

Do not block this gate because of subjective 4bpp image quality. Record image
quality observations for the EPD baseline measurement work.

## Factory recovery drill

The operator explicitly authorized the full factory recovery drill on
2026-08-11. The recovery used the preserved 16 MiB image at offset `0x000000`.
It did not write an eFuse, enable secure boot or enable flash encryption.

| Check | Result | Evidence or notes |
| --- | --- | --- |
| Source image size | PASS | 16,777,216 bytes |
| Source image SHA-256 | PASS | `47a3eb7b97a5a20dd0f3b0904b02ea792504c769941216af7dd4c02498b439d2` |
| Full-image write verification | PASS | The flash tool verified the written-data hash. |
| Independent readback | PASS | ROM bootloader, `--no-stub`, 16 segments of 1 MiB |
| Segment comparison | PASS | 16 of 16 segments matched byte for byte. |
| Reassembled readback size | PASS | 16,777,216 bytes |
| Reassembled readback SHA-256 | PASS | Exact match with the source image |
| Factory firmware boot | PASS | The operator confirmed the original interface and normal basic operation. |

The stub flasher stopped during two initial readback attempts. The original
backup log documents the same transport limitation. As a result, the controlled
readback used the ROM bootloader and the same 1 MiB segmentation method as the
two preservation reads. This method finished without an error.

Detailed recovery logs and readback binaries remain in the private backup
workspace. They are not part of this public repository.

## Exit rule

Platform issues #1 and #2 are closed. The U0 baseline is qualified for the
tested device. Continue with the next M1 platform-abstraction task without
changing the preserved factory backup.
