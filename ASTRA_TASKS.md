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
- [x] **Q1.3: Flipper Zero / Pebble Protocol Cross-Inspection**
  - Inspect CLI command parser against Flipper Zero CDC-ACM terminal robustness standards.
  - Verify durable sync engine cursor semantics under simulated sudden disconnects.
  - Completed bounded terminal rejection/editing, transient USB disconnect recovery and ESP-IDF 5.5.2 RX cleanup; documented Flipper/Pebble reference comparisons.
  - Verified fragment/commit interruption and cursor recovery in C++ and Kotlin, all 26 Host targets, 25 Android JVM tests, Android/ESP32-S3 builds and focused ASan/UBSan checks. Physical USB/BLE qualification remains hardware work.

---

## Milestone L1: Practical Launcher & CrossPoint Reader Core (实用化启动器与电子书阅读引擎)

- [x] **L1.1: 常驻系统状态栏、场景调度器与实机时钟进退 Bug 修复 (Status Bar, Scene Manager & Clock Fallback)**
  - Fix Clock Scene Entry Bug: In `ClockApplication::Enter`, when hardware RTC (`ReadRtc`) fails (e.g. uninitialized or absent PCF8563 I2C), fall back gracefully to `time_->Now()` (synchronized system time / monotonic fallback) rather than failing `Enter()` and silently bouncing back to Launcher; emit warning logs.
  - In `ApplicationRuntime::SwitchTo`, add explicit logging (`ESP_LOGW`/`ESP_LOGE`) on application entry failure before triggering `EnterLauncherFallback`.
  - Implement persistent top status bar: battery percentage & charging icon (ADC sampling), RTC system time, BLE and Wi-Fi state indicators.
  - Introduce Flipper Zero-inspired `SceneManager` and bounded `ViewPort` scheduler for robust, single-handed physical button navigation.
  - Decouple existing test demo scenes into structured push/pop application controller hierarchy.
  - Implemented a persistent 24px status bar, an eight-entry deferred SceneManager and a four-slot clipped ViewPort scheduler. Gallery/showcase/info/about now share the application runtime, retain menu selection and keep input active between preview frames.
  - Clock now survives RTC read failures with explicit system-time/uptime fallback and recovers on subsequent samples. New installations default to manual launching; existing settings, gray preclear and ordered shutdown are preserved.
  - Verified all 27 Host targets, ESP32-S3 firmware build, rendered UI previews and connected-device flash/boot smoke test. The status-only minute test transfers 44 bytes of panel RAM data; device boot also exercised the RTC voltage-low condition.
- [ ] **L1.2: CrossPoint 风格轻量文本与电子书阅读引擎 (E-Reader Engine)**
  - Implement CrossPoint-inspired (https://github.com/crosspoint-reader/crosspoint-reader) streamed plain-text and basic EPUB reader engine.
  - Support CJK character/word wrapping, line pagination, paragraph indentation, and dual font-size scaling.
  - Support NVS bookmark persistence, progress tracking, and integration with the C1 durable companion sync engine.
- [ ] **L1.3: 局域网 Web 传书与内容管理后台 (Direct Wi-Fi Content Ingestion)**
  - Implement lightweight embedded HTTP file transfer server using existing `zectrix_connectivity` Wi-Fi AP/STA mode.
  - Allow browser-based drag-and-drop file upload to SPI Flash / LittleFS storage.
  - Ensure Wi-Fi radio automatically powers down on completion to preserve battery.
- [ ] **L1.4: 桌面待机画报与锁屏仪表盘 (Ambient Sleep Cover & Dashboard)**
  - Render ambient sleep screen cover (daily calendar, reading progress, memo/quote art) before deep sleep.
  - Coordinate with Q1 peripheral power-down and pin hold states for microamp-level standby consumption.

---

## Post-L1 Autonomous Exploration Roadmap (后续自主架构拓展与衍生项目探索)

- [ ] **E1.1: CrossPoint & Flipper Zero 衍生项目调研与风格演进 (Firmware Forks & UI Architecture Study)**
  - Analyze open-source derivative forks: CrossPoint community forks (Biscuit, CrossMux, CrossInk) and Flipper Zero custom firmwares (Momentum, Unleashed).
  - Study their desktop layouts, sleep screen overlays, font caching/antialiasing/dithering for e-ink, and practical app launcher UX.
  - Propose and document architectural evolution for Zectrix Note4 open firmware.
