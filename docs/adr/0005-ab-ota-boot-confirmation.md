# ADR-0005: A/B application slots and bounded boot confirmation

Status: Implemented for M5.1 on 2026-09-08. Hardware fault injection remains open.

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

Persistent data stays in the existing 24 KiB NVS partition. The companion sync
record has a declared maximum of 5,376 bytes, alongside settings, identity and
BLE bond records. NVS also needs metadata and garbage-collection space. This
change does not claim additional NVS capacity or move existing data. Flash
from `0x912000` through `0xffffff` remains unallocated for future measured
storage requirements.

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

Transfer, image verification and boot-slot activation belong to M5.2 and later
update delivery work. M5.1 selects a safe destination and protects trial boots.
It does not expose a firmware download command or write an application image.

## Verification

Run `bash tools/test-update-service.sh` for partition selection, malformed
layouts, boot deadlines, confirmation failures, stale selection, native API
mapping and RTC watchdog control. Platform tests verify that startup failures
and teardown leave trial protection active. `bash tools/test-host.sh` includes
both targets.

An existing generated `sdkconfig` can retain old rollback settings. Enable
the settings shown in `sdkconfig.defaults`, or build a fresh configuration
without replacing local settings:

```bash
source tools/activate-dev-env.sh
idf.py --ccache -B build-ota -D "SDKCONFIG=$PWD/build-ota/sdkconfig" build
```

The firmware driver requires rollback and the inherited watchdog at compile
time. Without them, an unconfirmed image could reboot into itself or remain
stuck without a reset. The M5.1 build and Host tests pass with these settings.
On-device timeout, interrupted confirmation, power-loss rollback and both OTA
directions still require Note4 qualification. No hardware flash was performed
for this implementation.
