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
- [x] **L1.2: CrossPoint 风格轻量文本与电子书阅读引擎 (E-Reader Engine)**
  - Implement CrossPoint-inspired (https://github.com/crosspoint-reader/crosspoint-reader) streamed plain-text and basic EPUB reader engine.
  - Support CJK character/word wrapping, line pagination, paragraph indentation, and dual font-size scaling.
  - Support NVS bookmark persistence, progress tracking, and integration with the C1 durable companion sync engine.
  - Integrated a Storage-owned SPIFFS library with Launcher and Library -> Reading -> Options scenes, cancellable TXT/EPUB parsing, CJK pagination and 16px/24px fonts. Content installation is explicit; normal firmware flash preserves books.
  - Persisted eight recent bookmarks after successful display, including display recovery, and connected the latest reading position to C1 durable sync and Android's explicit resume action.
  - Verified all 28 Host targets with reader ASan/UBSan checks, 28 Android JVM tests and the debug build, the ESP32-S3 firmware build and connected-device flash/boot smoke. A 2 MiB TXT tail resume reads 63 source bytes. Physical reading controls and BLE progress exchange remain hardware qualification work.
- [x] **L1.3: 局域网 Web 传书与内容管理后台 (Direct Wi-Fi Content Ingestion)**
  - Implement lightweight embedded HTTP file transfer server using existing `zectrix_connectivity` Wi-Fi AP/STA mode.
  - Allow browser-based drag-and-drop file upload to SPI Flash / LittleFS storage.
  - Ensure Wi-Fi radio automatically powers down on completion to preserve battery.
  - Added SEND BOOKS with Mode -> Session scenes, WPA2 hotspot or saved-network access, and a self-contained browser library for drag-and-drop TXT/EPUB upload, download and deletion.
  - Reused the Storage-owned SPIFFS library with exclusive management, 1 KiB streaming writes, staged installation, duplicate-name protection and interrupted-upload cleanup. Sessions use screen access codes and stop on completion, cancellation, idle timeout or power/policy changes.
  - Verified all 29 Host targets, transfer ASan/UBSan checks, real HTTP and desktop/mobile browser flows, rendered device UI, ShellCheck, the ESP32-S3 build and connected-device flash/boot smoke. AP/STA transfer, physical controls, radio current and BLE/Wi-Fi coexistence remain hardware qualification work.
- [x] **L1.4: 桌面待机画报与锁屏仪表盘 (Ambient Sleep Cover & Dashboard)**
  - Render ambient sleep screen cover (daily calendar, reading progress, memo/quote art) before deep sleep.
  - Coordinate with Q1 peripheral power-down and pin hold states for microamp-level standby consumption.
  - Added SLEEP COVER with Choose -> Preview scenes, a saved dashboard/landscape/blank preference, daily calendar and original quotes, and the latest committed reading position. Unset clocks and failed preference writes remain explicit.
  - Shutdown presents a static full 1bpp cover with a white fallback and suppresses later status redraws. Existing peripheral cleanup and rail holds remain; bounded released-button detection arms GPIO18 EXT1 wake for USB deep sleep, with no periodic wake or refresh.
  - Verified all 30 Host targets, focused sleep/reader ASan/UBSan checks, rendered UI previews, ShellCheck, the ESP32-S3 firmware build and connected-device flash/boot smoke. Physical sleep/wake behavior and microamp standby current remain hardware measurements. See docs/SLEEP_COVER.md.

---

## Milestone S1: Modular Build, Kconfig Tailorability & Service Registry Decoupling (系统模块化积木与Kconfig裁剪解耦)

### 背景与决策意图 (Rationale & Context)
随着系统功能（Wi-Fi 直连、Web 传书后台、CrossPoint 电子书、USB 维护终端、A/B OTA 等）日益丰富，当前 `main/app_main.cc` 与 `main/CMakeLists.txt` 存在大一统硬编码依赖，缺乏灵活的可选裁剪机制。在 ESP32-S3 这类内存与 Flash 资源极其宝贵的嵌入式主控上，针对不同开发者场景（例如：纯离线阅读器、无屏幕调试网关、极简墨水屏时钟），系统必须支持“一键开/关模块”，且在关闭时通过链接器垃圾回收实现 0 字节 Flash 占用。
同时，充分信任并赋予 Astra 自主探索权，深度结合硬件 RTC 掉电保持电路与系统绝对时间维持，探索最优雅健壮的工程方案。

### 架构设计准则 (Architectural Principles)
1. **原生 Kconfig 驱动**：使用 ESP-IDF 原生的 `Kconfig.projbuild` 机制，暴露标准配置项，支持终端 `idf.py menuconfig` 和纯文本 `sdkconfig.defaults`（对 AI / CI 零成本配置友好）。
2. **CMake 动态组件过滤**：在 `main/CMakeLists.txt` 中依据 `CONFIG_ZECTRIX_ENABLE_*` 动态引入依赖，未选中的组件不参与编译与链接。
3. **轻量服务注册表 (Service Registry)**：引入无堆分配或极轻量的服务定位抽象（Interface-based），避免 `app_main.cc` 静态 `#include` 所有非必要头文件；上层 Launcher / UI 查询服务为 `nullptr` 时实现优雅降级。
4. **硬件 RTC 绝对时钟长效维持 (Persistent Wall Clock via RTC Circuit)**：深入分析硬件板载 PCF8563 独立 RTC 电路与备用供电机制。确保设备关机/Deep Sleep 期间 RTC 持续低功耗计时，开机时精准同步回系统墙上时钟（Wall Clock）而非单调运行时间（Monotonic/Uptime fallback），并结合 Companion BLE / 网络时间实现自动回写校准。

### 迭代任务清单 (Backlog Items)
- [ ] **S1.1: 基础服务抽象与轻量 Service Registry 设计**
  - 在 `components/zectrix_platform` 或核心库中定义标准化的纯虚服务接口基类与轻量服务注册表 (`zectrix_service_registry.h`)。
  - 规范各子系统的生命周期契约（`Init()`, `Start()`, `Stop()`），支持无堆或静态 slot 注册与解耦查询。
- [ ] **S1.2: 组件级 Kconfig 定义与 CMake 条件依赖绑定**
  - 为 `zectrix_connectivity`、`zectrix_reader`、`zectrix_cli`、`zectrix_update` 编写 `Kconfig.projbuild`。
  - 声明 `CONFIG_ZECTRIX_ENABLE_CONNECTIVITY`、`CONFIG_ZECTRIX_ENABLE_READER`、`CONFIG_ZECTRIX_ENABLE_USB_CLI` 等选项及其依赖拓扑（如 HTTP 依赖 Wi-Fi）。
  - 重构 `main/CMakeLists.txt` 为动态 `REQUIRES`，未开启的组件彻底从构建树中剪除。
- [ ] **S1.3: app_main 解耦与条件装配 (Conditional Wiring) & 硬件 RTC 深度集成**
  - 将 `main/app_main.cc` 中的具体业务类实例化改造为基于配置宏/服务注册表的装配逻辑。
  - Launcher / Menu 系统自动根据已启用的模块动态生成菜单项与场景导航，未启用的功能完全剥离。
  - 完善硬件板载 RTC（PCF8563）的初始化与绝对时钟恢复：关机与微安级休眠期间持续走时，开机自动复原真实世界墙上时间（绝对日期/时间戳），避免时钟归零或仅作为开机计时器。
- [ ] **S1.4: 极限轻量化配置档验证 (Minimal Profile Regression)**
  - 在 `tools/` 中新增最小化构建验证脚本（如纯离线 Minimal Profile 测试），验证禁用网络和阅读器后固件体积与片内 RAM 占用的削减效果（目标减少 30%+ 固件体积）。
  - 确保全套 Host 测试与实机烧录冒烟测试在全量（Full）与最小化（Minimal）两种配置模式下均 100% 正常工作。

---

## Post-L1 Autonomous Exploration Roadmap (后续自主架构拓展与衍生项目探索)

- [ ] **E1.1: CrossPoint & Flipper Zero 衍生项目调研与风格演进 (Firmware Forks & UI Architecture Study)**
  - Analyze open-source derivative forks: CrossPoint community forks (Biscuit, CrossMux, CrossInk) and Flipper Zero custom firmwares (Momentum, Unleashed).
  - Study their desktop layouts, sleep screen overlays, font caching/antialiasing/dithering for e-ink, and practical app launcher UX.
  - Propose and document architectural evolution for Zectrix Note4 open firmware.
