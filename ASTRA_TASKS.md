# Zectrix Note4 Platform - Astra Autonomous Iteration Backlog

This backlog guides the continuous autonomous iteration loop for `gpt-6-astra`.
Each iteration picks the top unfinished task, implements production code, verifies host builds, makes a clean git commit, and checks off the task.

---

## Milestone C1: Connectivity Platform (互联平台)

- [x] **C1.1: Direct Wi-Fi Backend Interface & Drivers**
  - Define `zectrix_wifi_backend.h` and implement `zectrix_wifi_esp_driver.cc`.
  - Wire ESP-IDF Wi-Fi/netif events with thread-safe callbacks.
  - Harmonize RF self-test and connectivity Wi-Fi sharing.
- [x] **C1.2: Direct HTTPS Resource Client & Escalation Path**
  - Implement `zectrix_wifi_http.cc` using `esp_http_client`.
  - Wire `ConnectivityPolicy` to trigger direct Wi-Fi fetch when phone proxy is unavailable.
  - Implement automatic radio shutdown (power save) after transfer completion.
- [x] **C1.3: Companion Sync Protocol Hardening & Durable Reconnect**
  - Verify sequence cursor exchange on BLE reconnect.
  - Ensure unacknowledged durable mutations survive disconnects.
  - Implement single-use NFC enrollment token verification flow.

---

## Milestone D1: Maintenance CLI (维护命令行系统)

- [x] **D1.1: USB CDC-ACM Session & Transport Layer**
  - Define `zectrix_cli_usb.h` and `zectrix_cli_session.h`.
  - Non-blocking line parser and ANSI-capable text session.
- [x] **D1.2: Platform Diagnostic Command Set Implementation**
  - Implement `sysinfo`, `heap`, `tasks`, and `uptime` inspection commands.
  - Implement `epd-inspect` (framebuffer and refresh status dump).
  - Implement `log-stream` tap without disturbing the main EPD refresh loop.
- [x] **D1.3: Host Integration & Tooling Verification**
  - Implement interactive host simulation for CLI testing.

---

## Milestone M5: Update Architecture & Partition Management (OTA 体系)

- [x] **M5.1: A/B OTA Partition Verification & Rollback Policy**
  - Verify active running partition vs update partition layout.
  - Implement rollback watchdog on unconfirmed boot.
- [x] **M5.2: Streamed Firmware Chunk Verification**
  - CRC-32 and image header integrity check before commit.

---

## Milestone R1: Advanced E-Ink Waveform & Quality Engine (墨水屏进阶渲染)

- [x] **R1.1: Partial Refresh Dirty Region Optimizer**
  - Screen diffing and minimal bounding box calculation.
- [x] **R1.2: Ghosting Mitigation & Adaptive Full Refresh Cycle**
  - Frame count threshold and high-contrast refresh triggers.

---

## Quality & Continuous Regression Loop (持续质量审查与已有功能复查)

- [x] **Q1.1: Comprehensive Contract Regression & Host Test Suite Verification**
  - Run full suite (`tools/test-host.sh`) ensuring all 26 test targets pass with zero failures.
  - Audit thread-safety across new Wi-Fi and CLI background tasks.
- [x] **Q1.2: Low-Power Lifecycle & Radio Coexistence Audit**
  - Verify Wi-Fi modem completely powers off after HTTP burst transfer.
  - Review `PowerOff()` clean shutdown sequence ensuring zero leaked SPI/I2C peripheral state.
  - Implemented ordered peripheral cleanup, NFC/audio task joins and rail-off GPIO holds.
  - Verified all 26 Host targets, sanitizer checks and the ESP32-S3 build; physical current and RF coexistence measurements remain hardware qualification work.
- [ ] **Q1.3: Flipper Zero / Pebble Protocol Cross-Inspection**
  - Inspect CLI command parser against Flipper Zero CDC-ACM terminal robustness standards.
  - Verify durable sync engine cursor semantics under simulated sudden disconnects.
