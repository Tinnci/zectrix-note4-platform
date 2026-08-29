# M4 SDK v1 exit gate

Status: PASS on firmware commit `91043ea`.

M4 closes only when all checks in this document pass on one pushed commit.
Closure freezes the SDK v1 source contract. It does not promise a binary ABI,
dynamic loading, OTA compatibility, or partition compatibility.

## Contract gate

- SDK version is 1.0.0.
- The public-header set matches `tools/check-sdk-v1.sh`.
- Public SDK headers expose no ESP-IDF, FreeRTOS, board, driver, Platform, or
  opaque factory-context type.
- The locked consumer and minimal example compile with public headers only.
- Launcher, Clock, Settings, and Diagnostics use SDK v1 lifecycle and input.
- The old M3 application headers and factory-context path do not exist.
- Documentation and headers specify the same ownership and error rules.

## Automated gate

Run:

```bash
for test in tools/test-*.sh; do "$test"; done
tools/check-architecture-boundaries.sh --self-test
tools/check-architecture-boundaries.sh
tools/build-firmware.sh --clean
git diff --check
```

Record the application image size and free app-partition capacity. Compare the
same-boot platform and runtime heap checkpoints. M4 does not set an arbitrary
heap target; an unexplained regression blocks closure.

## Hardware gate

Flash the clean-build candidate and verify all written hashes. Then verify:

1. Repeated Launcher navigation and selection.
2. Clock entry, minute update, Home, and shutdown input.
3. Settings load, change, save, reboot, and persisted reload.
4. Diagnostics menu, individual test, complete test sequence, and cancellation.
5. Auto Showcase, Gallery, Device Info, and About private transition adapters.
6. More than eight partial refresh requests and automatic full recovery.
7. A 4 bpp Gallery transition followed by a safe 1 bpp baseline rebuild.
8. Idle behavior with automatic showcase enabled and disabled.
9. Shutdown rail sequence, deep sleep, and wake into a fresh lifecycle.

The device must show no panic, watchdog, unexpected reset, EPD BUSY timeout,
or new functional regression.

## Qualification record

The strict host suite, SDK checker and self-test, architecture checker and
self-test, and ESP-IDF v5.5.2 fullclean build passed on 2026-08-12. The
application image was `0xf9dd0` bytes. The smallest application partition had
67 percent free.

The clean commit image reported `v1.0.0-34-g91043ea`. It was written to the
qualified ESP32-S3 at `/dev/ttyACM1`. The bootloader, partition table, and
application writes passed the esptool hash check. Boot completed with no panic,
watchdog, or unexpected reset. Board, RTC, NFC, PSRAM, and the application
runtime initialized.

The same-boot internal-heap checkpoints were:

| Checkpoint | Free bytes | Minimum free bytes | Largest block bytes |
| --- | ---: | ---: | ---: |
| M2-equivalent platform | 244827 | 244827 | 163840 |
| SDK v1 runtime active | 244803 | 244803 | 163840 |

The runtime delta was 24 bytes at this checkpoint. The largest internal block
did not change. The user confirmed that all functional hardware tests passed
after the firmware update.
