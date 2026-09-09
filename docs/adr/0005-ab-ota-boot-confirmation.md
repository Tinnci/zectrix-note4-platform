# ADR-0005: A/B application slots and bounded boot confirmation

Status: Implemented for M5.1 and M5.2 on 2026-09-08. Hardware fault injection remains open.

## Context and measurements

The previous layout had one 3 MiB factory application and no OTA selection
data. A safe update needs an inactive destination and a bootloader that rejects
an unconfirmed image after a reset. The existing factory and NVS locations must
remain usable during the transition.

The M5.1 ESP32-S3 build with ESP-IDF 5.5.2 produces a 1,519,296-byte application
and a 21,136-byte bootloader. Each 3,145,728-byte application slot has 1,626,432
bytes free, about 52%. The four embedded asset files total 91,297 bytes and
are already part of the application image. These are measurements, not a fixed
image-size requirement.

The M5.2 build produces a 1,524,880-byte application with the same bootloader.
Each application slot has 1,620,848 bytes free, about 52%. Its generated binary
also passes the Host streamed-transfer test with 997-byte chunks.

Persistent data stays in the existing 24 KiB NVS partition. The companion sync
record has a declared maximum of 5,376 bytes, alongside settings, identity and
BLE bond records. NVS also needs metadata and garbage-collection space. This
change does not claim additional NVS capacity or move existing data. Flash
from `0x912000` through `0xffffff` was unallocated by M5. L1.2 uses the first
4 MiB of that space for the book filesystem; `0xd12000` through `0xffffff`
remains unallocated. See [the reader storage notes](../READER.md).

## Partition decision

Use the native ESP-IDF OTA scheme on the 16 MiB Note4:

| Partition | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | Existing persistent data |
| `phy_init` | `0xf000` | `0x1000` | Existing PHY data |
| `factory` | `0x10000` | `0x300000` | Initial application and recovery fallback |
| `ota_0` | `0x310000` | `0x300000` | Application slot A |
| `ota_1` | `0x610000` | `0x300000` | Application slot B |
| `otadata` | `0x910000` | `0x2000` | Native redundant OTA selection records |
| `books` | `0x912000` | `0x400000` | L1.2 SPIFFS book library; explicit content flash only |

`UpdateService` belongs to Platform and runs on the application owner. Its ESP
adapter copies at most 32 partition descriptors. Validation checks flash bounds,
sector and application alignment, overlaps, duplicate roles, two writable OTA
slots and at least two writable OTA metadata sectors. Bounds use the smaller
of configured and physical flash size.

The running image must match a table entry. The next destination must match
ESP-IDF's inactive slot: factory to A, A to B, and B to A. A selected boot
partition different from the running partition disables update selection.
This can indicate an already scheduled update or bootloader fallback. It must
not cause the selected image to be overwritten or confirmed by the old app.
Image size is checked against the destination on each `SelectUpdateTarget()`
call. No raw partition pointer is exposed to applications or SDK v1.

## Boot confirmation and rollback

Enable native bootloader rollback and retain its RTC watchdog through
`app_main()`. Fresh defaults give the bootloader 60 seconds. `BeginBoot()`
arms a 60-second application deadline before board and service initialization.
Repeated calls do not extend that deadline. The ESP32-S3 driver uses the RTC
watchdog HAL because the legacy `rtc_wdt_*` API does not support this target.
The hardware resets the system without a timer task or application polling.

A native `PENDING_VERIFY` image remains unconfirmed until Platform initialization,
the splash refresh, runtime startup and the first launcher render all succeed.
The composition root then calls `ConfirmBoot()`. It checks the deadline and
reads the partition selection again. The adapter also checks that the running
and selected image match immediately before marking the image valid.

`esp_ota_mark_app_valid_cancel_rollback()` commits `VALID` before the watchdog
is disabled. A failed read, failed confirmation write, changed boot selection,
timeout or abandoned initialization leaves reset protection active. Object
destruction never cancels this protection. After a reset or power loss before
confirmation, the native bootloader marks the pending image `ABORTED` and
selects a previous valid OTA image or the factory fallback.

Factory and already valid images do not require another metadata write. They
release the watchdog during startup. A known-good recovery image can run when
OTA layout validation fails, but update target selection stays unavailable.
`NEW`, `INVALID`, `ABORTED`, unknown and undefined OTA states are not accepted
as a verified startup. A missing valid fallback is reported. A healthy trial
can still confirm itself, but an unhealthy trial cannot be promised recovery
when all fallback images are missing or damaged.

## Compatibility and installation

The first transition from the factory-only layout requires the complete new
ESP-IDF flash image set, including the bootloader and partition table. An
application-only transfer cannot install this layout or add bootloader rollback.
The generated `flash_args` includes blank `ota_data_initial.bin` at `0x910000`.
A full development flash therefore resets OTA selection to factory. It does
not erase the preserved NVS partition.

Normal OTA transfers must keep the installed bootloader, partition table,
factory image and persistent data locations compatible. Rollback changes the
application image only. It does not undo NVS writes, so trial firmware must
keep persistent records readable by its fallback firmware. This iteration
does not change their formats or enable eFuse anti-rollback.

M5.2 adds streamed image verification, inactive-slot writes and boot selection
through `Platform::Update()`. Update delivery still needs a transport and user
workflow. No firmware download command or automatic restart is added.

## Streamed firmware verification

The platform owner uses the same `UpdateService` for boot protection and one
firmware transfer at a time. All methods are synchronous and belong to the
owner task. They do not expose native partition pointers or OTA handles.

1. `BeginFirmware(image_bytes, image_crc32)` records the complete raw ESP-IDF
   application binary size and CRC. The size includes any native digest,
   signature and padding. It must fit the inactive slot. Startup must already
   be confirmed and the running and selected boot partitions must still match.
   This call does not erase flash.
2. `WriteFirmwareChunk(offset, data, size, chunk_crc32)` accepts 1–4,096 bytes
   at the next exact offset. CRC-32 covers all bytes in this chunk. The caller
   keeps the buffer stable until the call returns. A bad argument, unexpected
   offset or bad chunk CRC leaves progress unchanged, so the caller can send
   the correct next chunk. A duplicate chunk is rejected rather than written
   again.
3. The service buffers the first 288 bytes across any chunk boundaries. The
   ESP adapter checks the image magic, segment count, flash header fields,
   native digest flag, first segment length and application descriptor magic.
   IDF checks the chip ID and supported chip revisions. Invalid headers are
   rejected before `esp_ota_begin()`. Valid headers start sequential OTA writes.
   IDF erases sectors as needed and handles flash encryption alignment.
4. `CommitFirmware()` first requires the exact declared byte count and the
   cumulative image CRC. `esp_ota_end()` flushes and validates the image using
   the existing native segment, checksum, digest and signature policy. An
   additional metadata read uses the received byte count as its partition
   bound. This rejects a truncated image that would otherwise reuse an old
   tail beyond the received data in the inactive slot.
5. The adapter reads the written image back through a 1,024-byte buffer and
   compares its CRC. It checks the running and selected boot identities again,
   then calls `esp_ota_set_boot_partition()`. Success schedules the verified
   image as `NEW` for the next boot. M5.1 then requires trial-boot confirmation.
   The caller controls when to restart.

Both CRC values use IEEE CRC-32: reflected polynomial `0xedb88320`, initial
register and final XOR `0xffffffff`. `FirmwareCrc32()` starts with a previous
value of zero and accepts the finalized value from the preceding call for
incremental calculation. The standard `123456789` vector gives `0xcbf43926`.
CRC detects transmission and flash corruption. It does not authenticate a
publisher. A delivery workflow must obtain the image and its expected metadata
from its trusted source. IDF's configured signature policy remains in force.

`ReadFirmwareStatus()` returns copied progress. Received bytes can include a
header still buffered in RAM. They are not a durable reconnect cursor. A short
stream returns `kIncompleteImage` at commit and can continue. Header, image CRC,
native validation, flash or changed-target failures terminate the transfer and
release its OTA handle. `AbortFirmware()` and service/backend destruction also
release an unfinished writer. Retrying a failed or aborted transfer starts at
offset zero. No transfer operation confirms the running firmware or cancels
its boot watchdog.

Errors before boot selection leave the running image selected. A metadata I/O
error during selection can occur after the new selection has persisted. The
service reports failure, consumes the writer and rechecks the actual boot
selection before any new transfer, so it cannot overwrite an already scheduled
image. Native redundant OTA records retain responsibility for interrupted
metadata writes. Aborting after a successful commit does not undo selection.

## Verification

Run `bash tools/test-update-service.sh` for partition selection, malformed
layouts, boot deadlines, confirmation failures, stale selection, native API
mapping and RTC watchdog control. The same target tests streamed writes across
header boundaries, CRC vectors, retryable chunks, size limits, incomplete
streams, malformed headers, native validation, bounded metadata reads, corrupted
flash, partial write failures, interrupted selection and handle cleanup.
Platform tests verify that startup failures and teardown leave trial protection
active. `bash tools/test-host.sh` includes both targets.

An existing generated `sdkconfig` can retain old rollback settings. Enable
the settings shown in `sdkconfig.defaults`, or build a fresh configuration
without replacing local settings:

```bash
source tools/activate-dev-env.sh
idf.py --ccache -B build-ota -D "SDKCONFIG=$PWD/build-ota/sdkconfig" build
bash tools/test-update-service.sh build-ota/zectrix_epd_demo.bin
```

The optional binary argument streams the actual build through the Host driver
with 997-byte chunks. It exercises header compatibility and CRC/readback with
the production service and adapter. SDK flash, image-verifier and boot-selection
calls are faked on Host. This does not replace native or hardware qualification.

The firmware driver requires rollback and the inherited watchdog at compile
time. Without them, an unconfirmed image could reboot into itself or remain
stuck without a reset. The M5.1 and M5.2 builds and Host tests pass with these
settings. M5.2 passes all 26 Host targets and ShellCheck for `tools/*.sh`.
On-device interrupted writes, flash readback, timeout, interrupted confirmation,
power-loss rollback and both OTA directions still require Note4 qualification.
No hardware flash was performed for these implementations.
