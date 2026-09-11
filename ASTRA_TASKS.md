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
- [x] **S1.1: 基础服务抽象与轻量 Service Registry 设计**
  - 在 `components/zectrix_platform` 或核心库中定义标准化的纯虚服务接口基类与轻量服务注册表 (`zectrix_service_registry.h`)。
  - 规范各子系统的生命周期契约（`Init()`, `Start()`, `Stop()`），支持无堆或静态 slot 注册与解耦查询。
  - Added pure virtual Service/ServiceProvider interfaces and a 16-slot typed registry with no RTTI, task or registry heap allocation. Missing, unstarted and stopped providers return null; failed startup unwinds attempted providers in reverse order.
  - Integrated ten embedded lifecycle bindings into production Platform startup, accessors and cleanup. Preserved CLI/NFC cleanup, final Input/Power ownership and trial-boot watchdog behavior; Kconfig selection and application conditionals remain S1.2/S1.3.
  - Verified all 31 Host targets, focused registry/platform ASan/UBSan checks, ShellCheck, the ESP32-S3 build and connected-device flash/boot smoke. Registry size is 528 bytes on the 64-bit Host and 264 bytes in the ESP32-S3 ELF; registry dispatch allocates no heap. See docs/SERVICE_REGISTRY.md.
- [x] **S1.2: 组件级 Kconfig 定义与 CMake 条件依赖绑定**
  - 为 `zectrix_connectivity`、`zectrix_reader`、`zectrix_cli`、`zectrix_update` 编写 `Kconfig.projbuild`。
  - 声明 `CONFIG_ZECTRIX_ENABLE_CONNECTIVITY`、`CONFIG_ZECTRIX_ENABLE_READER`、`CONFIG_ZECTRIX_ENABLE_USB_CLI` 等选项及其依赖拓扑（如 HTTP 依赖 Wi-Fi）。
  - 重构 `main/CMakeLists.txt` 为动态 `REQUIRES`，未开启的组件彻底从构建树中剪除。
  - Added four component Kconfig definitions with selectable connectivity, Wi-Fi, HTTPS, Web transfer, reader, USB CLI and firmware writing. Native Kconfig resolution runs before IDF dependency expansion, preserving defaults and saved settings while removing excluded components, sources, fonts and book storage.
  - Integrated selected services and Launcher destinations, independent Web transfer, offline bookmark persistence and RF SKIP reporting. Moved existing boot validation, rollback confirmation and watchdog protection into the mandatory System component so disabling firmware writing preserves recovery safety.
  - Verified all 32 Host targets, five Launcher profiles and five Platform compositions, six ESP32-S3 firmware profiles, a fresh core build, saved-config off/on/off changes, ShellCheck and connected-device flash/boot smoke. Full firmware is 2,990,112 bytes; Core is 545,216 bytes (81.8% smaller). See docs/MODULAR_BUILD.md; broader application/RTC work remains S1.3 and reusable minimal-profile qualification remains S1.4.
- [x] **S1.3: app_main 解耦与条件装配 (Conditional Wiring) & 硬件 RTC 深度集成**
  - 将 `main/app_main.cc` 中的具体业务类实例化改造为基于配置宏/服务注册表的装配逻辑。
  - Launcher / Menu 系统自动根据已启用的模块动态生成菜单项与场景导航，未启用的功能完全剥离。
  - 完善硬件板载 RTC（PCF8563）的初始化与绝对时钟恢复：关机与微安级休眠期间持续走时，开机自动复原真实世界墙上时间（绝对日期/时间戳），避免时钟归零或仅作为开机计时器。
  - Split the terminal entry, shell, composition and concrete applications. A fixed catalog supplies both Launcher labels and runtime targets; Kconfig excludes optional source files and absent services omit their destinations.
  - Moved RTC restoration before Connectivity startup and unified Clock/status/sleep time snapshots. Added an offline View/Edit clock scene and authorized Companion Hello calibration, with current-session/age checks and foreground RTC ownership.
  - Hardened PCF8563 reads and STOP-protected calendar/offset saves, retained valid time during persistence failures, and added bounded retries. Initialization disables unused CLKOUT; normal shutdown preserves the running calendar. Documented the external backup-supply requirement without claiming unmeasured board current or retention.
  - Verified all 32 Host targets, 29 Android JVM tests and the debug build, five ESP32-S3 firmware profiles and connected-device flash/boot smoke. Full is 2,995,520 bytes and Core is 548,784 bytes. Host tests cover restart restoration and interrupted calibration; the device smoke exercised invalid retained-clock fallback. Physical backup retention, standby current and phone calibration remain hardware qualification work. See docs/TIME.md.
- [x] **S1.4: 极限轻量化配置档验证 (Minimal Profile Regression)**
  - 在 `tools/` 中新增最小化构建验证脚本（如纯离线 Minimal Profile 测试），验证禁用网络和阅读器后固件体积与片内 RAM 占用的削减效果（目标减少 30%+ 固件体积）。
  - 确保全套 Host 测试与实机烧录冒烟测试在全量（Full）与最小化（Minimal）两种配置模式下均 100% 正常工作。
  - Added committed Full/Minimal profiles, isolated repeatable builds and `tools/test-minimal-profile.sh` with actual binary/ESP-IDF RAM reports. Checks cover module/source exclusion, retained boot protection and identical OTA/book partitions; saved developer configuration remains untouched.
  - Full firmware is 2,995,728 bytes; Minimal is 548,864 bytes (81.7% smaller). Static internal RAM falls from 213,495 to 118,651 bytes (44.4%); its data/BSS subset falls from 72,168 to 32,156 bytes (55.4%). See docs/MODULAR_BUILD.md.
  - Verified all 32 Host targets including Full/Minimal compositions, nine module/profile cases, fresh firmware builds with contradictory saved settings, ShellCheck and both connected-device flash/boot smokes. Readiness now confirms the first Launcher frame, boot confirmation and selected catalog/CLI. The device finished on Full; physical RTC retention and standby-current measurement remain separate hardware work.

---

## Post-L1 Autonomous Exploration Roadmap (后续自主架构拓展与衍生项目探索)

- [x] **E1.1: CrossPoint & Flipper Zero 衍生项目调研与风格演进 (Firmware Forks & UI Architecture Study)**
  - Analyze open-source derivative forks: CrossPoint community forks (Biscuit, CrossMux, CrossInk) and Flipper Zero custom firmwares (Momentum, Unleashed).
  - Study their desktop layouts, sleep screen overlays, font caching/antialiasing/dithering for e-ink, and practical app launcher UX.
  - Propose and document architectural evolution for Zectrix Note4 open firmware.
  - Reviewed Biscuit, CrossMux, CrossInk, Momentum and Unleashed source with CrossPoint/Flipper references. Documented daily Home, scene ownership, static versus active standby, streamed transfer, font preparation and image dithering decisions in [docs/FIRMWARE_UI_STUDY.md](docs/FIRMWARE_UI_STUDY.md).
  - Added a proportional Launcher overflow indicator on the existing canvas, hidden when all eight rows fit. No new task, buffer, persistent state or refresh operation is required. Added the Minimal menu to the visual preview fixture.
  - Verified all 32 Host targets, the focused display suite, Full/Minimal ESP32-S3 builds and rendered first/last/Minimal menus. Full is 2,995,824 bytes and Minimal is 548,960 bytes (81.7% smaller); static internal RAM is 213,495/118,651 bytes. Hardware was not required for this UI change; proposed home, font and cover features remain subsequent iterations.
- [x] **E1.2: 磁贴仪表盘主屏演进 (Tile Dashboard & Overview Card)**
  - 基于 E1.1 对 Biscuit 和 Flipper 桌面架构的调研，设计并实现卡片化主屏体验。
  - 探索将主屏划分为系统状态/阅读概览卡片与多功能应用磁贴（App Tiles）。
  - 支持单手物理按键直观导航与焦点切换，自适应模块动态装配状态。
  - Added a calendar/reading overview, six catalog-driven Home tiles and a private Tools scene. UP/DOWN follows one circular focus order, selections survive returns, and trimmed modules reflow into fewer rows. The existing canvas, status viewport and refresh ownership remain in use.
  - Continue Reading restores the latest committed local bookmark through cancellable Library/Reading scenes. Missing books, stale file lengths and invalid positions return to Library with an explanation; failed display commits preserve saved progress. See [docs/HOME.md](docs/HOME.md).
  - Verified all 32 Host targets, reader ASan/UBSan with and without connectivity, rendered Home variants, ShellCheck and Full/Minimal ESP32-S3 builds. Full is 3,000,736 bytes and Minimal is 553,200 bytes (81.6% smaller); adjacent tile focus transfers 3,456 bytes of panel RAM data. Device smoke was not needed for this UI change; physical contrast and latency remain hardware measurements.
- [x] **E1.3: 墨水屏高响应度调度与防卡死保护 (Display Responsiveness & Input Concurrency)**
  - 借鉴 Flipper Zero ViewPort 锁管理与事件流机制，优化慢速 EPD 刷新与高频输入的并发处理。
  - 在硬件 BUSY 传输期间探索非阻塞更新尝试、脏区域合并（Dirty Region Merge）或跳帧策略。
  - 确保高频按键与时间更新下输入队列不阻塞、界面无假死，保持流畅单手操控手感。
  - Completed bounded foreground input bursts, merged content/status updates and priority-preserving button buffering. Confirmation, Back and application transitions end a burst; shutdown supersedes queued actions. Existing streamed reading, successful-display bookmark persistence and single-owner display access remain in use.
  - Bounded driver lock/BUSY waits and gray-failure rail cleanup prevent endless waits and unsafe recovery commands. Extended runtime reentry protection through factories, destruction and shutdown/failsafe delegates; SDK 1.1.1 preserves source compatibility.
  - Verified all 32 Host targets, runtime ASan/UBSan, Full/Minimal ESP32-S3 builds and the connected-device Full flash/boot smoke. Nine inputs queued during an 800 ms simulated BUSY period require one subsequent refresh instead of nine, reducing simulated backlog drain from 8.1 s to 0.9 s. See [docs/DISPLAY_RESPONSIVENESS.md](docs/DISPLAY_RESPONSIVENESS.md); physical input latency and panel quality remain hardware measurements.
- [x] **E1.4: 交互状态机一致性与导航流整合 (Unified Navigation Consistency)**
  - 统一全局按键交互范式（列表/磁贴切换、确认进入、长按返回上一级场景、全局快捷休眠等）。
  - 规范各微应用间栈式调度（Push/Pop Scene）与退出清理契约，确保所有子页面行为逻辑高度一致。
  - Completed shared button intents and one-level Back across all first-party applications. Root Back restores the Home/Tools parent and focus; explicit Home and entry-failure fallback retain their existing behavior.
  - Integrated Connectivity action/forget scenes, Diagnostics run/cancel/summary navigation, Clock scene refresh recovery and idempotent controller cleanup. Preserved streamed reading, committed bookmarks, transfer service ownership and static shutdown covers. See [docs/NAVIGATION.md](docs/NAVIGATION.md).
  - Verified all 32 Host targets, focused navigation ASan/UBSan, rendered connection menus and Full/Minimal ESP32-S3 builds. Device smoke was not required for this application/navigation change; physical button flows remain hardware qualification work.
- [x] **E1.5: 2.4GHz 射频协同仲裁机制 (Radio Arbiter for Wi-Fi & BLE)**
  - 针对 ESP32-S3 单天线共享架构，设计 Wi-Fi 高速传书与 BLE 伴侣同步的协同调度状态机。
  - 优化无线突发工作时的电源与射频资源分配，确保无线切换平滑可靠。
  - Added an allocation-free RadioArbiter on the existing Connectivity session owner. Wi-Fi bursts admit new outbound durable frames every 250 ms; book sessions restore normal sync after 500 ms of inactivity or immediately after radio release.
  - Preserved pairing, authorization, ACK/NACK, in-flight retries, phone confirmation ownership and failed-cleanup claims. Kept STA modem sleep and separated Wi-Fi/BLE stacks across S3 cores. See [docs/RADIO_ARBITER.md](docs/RADIO_ARBITER.md).
  - Verified all 33 Host targets, focused radio ASan/UBSan, Full/Minimal/BLE-only ESP32-S3 builds and connected-device flash/boot smoke. Simulated 256 KiB AP/STA upload/download runs sustain bidirectional sync; eight outbound states converge in 2000 ms. Physical RF throughput, BLE latency and current remain hardware measurements.
- [x] **E1.6: 视觉呈现精细化与交互打磨 (Visual Refinement & Ergonomic Polish)**
  - 探索主屏顶栏的中立与极简呈现（如标题去品牌化、平衡居中构图，探索 `HOME` 或 `NOTE4 | HOME` 等中立命名）。
  - 针对大字号阅读排版探索行间距优化（如适度扩大行距以提供更多呼吸感），消除 400x300 墨水屏上连续多行 CJK 排版的视觉黏连感。
  - 优化待机画报底部唤醒提示（`Press DOWN to wake`）的视觉辨识度（探索加粗或高对比点阵字型），提升弱光或反光环境下的易读性。
  - 探索磁贴图标精细化演进，研究将基于文字符号拼接的磁贴标识升级为 16x16 紧凑单色位图图标（Bitmap Glyph），赋予启动器磁贴更细腻纯粹的极客质感。
  - Centered the neutral HOME title and added seven original 16x16 monochrome tile glyphs using 224 bytes of constant bitmap data. Increased 24px reader line spacing from 28px to 32px while retaining seven rows and source-byte anchors.
  - Added a high-contrast wake hint to final dashboard/landscape covers, retaining preview controls and the entirely white privacy cover. Reused the shared canvas and existing scene, display and shutdown ownership. See [docs/HOME.md](docs/HOME.md), [docs/READER.md](docs/READER.md) and [docs/SLEEP_COVER.md](docs/SLEEP_COVER.md).
  - Verified all 33 Host targets, focused display integration, reader ASan/UBSan with and without connectivity, Full/Minimal ESP32-S3 builds and rendered Home/reader/sleep variants. Device smoke was not required for this UI change; physical low-light readability and panel contrast remain hardware measurements.
- [x] **E1.7: 系统级原生中文与多语言框架演进 (System-wide Localization & Language Architecture)**
  - **核心目标**：打破目前“仅书籍数据支持中文、系统界面全英文”的局限，让状态栏、启动器磁贴、设置与导航提示具备完整的中文与国际化呈现能力。
  - **自主方案探索**：
    - 赋予 Astra 充分的设计自主权，对比并探索嵌入式环境下的最佳多语言方案（例如：编译期静态语言表、轻量运行时 Catalog 字典、或与现有应用清单绑定的本地化机制等），选取对 ESP32-S3 Flash/RAM 开销最小、最优雅的实现。
  - **工程约束与设计原则**：
    - 零堆分配原则：字符查询与渲染不得在主绘制循环中引入动态堆内存分配；
    - 模块化裁剪兼容：继承 S1 架构，保持通过 Kconfig 自由裁剪语言包和字库的能力（极简配置下仍可剥离未使用的中文字库）；
    - 墨水屏视觉自适应：中文文本排版须严格契合 400x300 点阵布局与物理按键提示边界，避免文字截断或排版错位。
  - Added 339 static Chinese/English strings, catalog-bound application labels and a private Settings language picker. Full defaults to Chinese; explicit choices persist through StorageService, with immediate content/status redraw, save-failure retry and one-level Back.
  - Unified UTF-8 measurement, drawing and scalar-safe fitting without heap allocation. Chinese UI reuses Reader's font or a 355-glyph, 12,425-byte subset independently of Reader. Minimal excludes both Chinese font sources. See [docs/LOCALIZATION.md](docs/LOCALIZATION.md).
  - Verified all 34 Host targets, localization ASan/UBSan across three font/language compositions, rendered Chinese/English screens, ShellCheck and Full/Minimal/Chinese-without-Reader ESP32-S3 builds. Full is 3,018,304 bytes; Minimal is 562,752 bytes (81.4% smaller). Adding Chinese to the offline Minimal configuration costs 21,504 firmware bytes and 8 static RAM bytes. Device smoke was not required for this UI change; physical panel readability remains a hardware measurement.


- [x] **E1.8: USB 宿主通信与设备管理架构演进 (Host-Device USB Communication & Management Architecture)**
  - **背景与核心愿景**：
    - 当前 Note4 的 USB 接口局限于只读维护终端，尚未形成完整的数据导入导出与设备控制闭环。
    - 演进愿景是将 USB 接口提升为兼顾“极客交互控制”与“便捷数据流转”的统一宿主通信桥梁（Unified Host Bridge），让日常图书传输、文件管理与系统配置触手可及。
  - **交由 Astra 深度推演的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *宿主接入与传输范式*：如何权衡块设备物理磁盘挂载（如 MSC/虚拟 U 盘）、对象级协议（如 MTP）与串行会话流协议（如 CDC-ACM 双模/WebSerial）在跨平台（Mac/Win/Linux/Android）免驱可用性、Flash 文件系统并发安全性与单片机资源开销之间的深层关系？
    2. *统一会话与模式流转*：如何在单一物理连接下，自然融合面向人类的字符交互（命令行设置、状态探查、文件浏览）与面向主机的批量数据流吞吐（文件分块推拉、校验）？是否存在低开销、低摩擦的会话协商与流式切换机制？
    3. *端侧视觉与人机交互协同*：当 USB 正在发生数据交换或配置变更时，墨水屏视觉层与三键状态机应如何优雅配合（例如专属传输看板 vs 呼吸式轻量反馈）？如何在各种链路状态下确保物理按键操控的确定性与流畅感？
  - **期待产出**：
    - 赋能 Astra 结合开源优秀实践（Flipper Zero, CrossPoint, Android ADB 等）与 Note4 既有架构基础设施，深入权衡并提出最符合掌上随身墨水屏特质的最佳实践工程方案。
  - Compared MSC/MTP/serial access, CrossPoint's SD/USB controller handoff and Flipper's explicit RPC sessions. Selected framed object operations over the existing USB Serial/JTAG transport; documented host compatibility, ownership, wire format and extension choices in [docs/USB_HOST.md](docs/USB_HOST.md).
  - Added optional Tools > USB Manager and a uv/pyserial host tool for paged book lists, bounded TXT/EPUB import/export and language/showcase/sleep-cover settings. One copied request slot serves the foreground owner; existing storage leases, staging, no-overwrite commit and deterministic cancel/Back/shutdown cleanup remain authoritative. Binary errors cannot fall through to terminal commands; uncertain mutations are never automatically retried.
  - Verified all 35 Host targets, USB ASan/UBSan with seven Python client/integration scenarios, Chinese/English rendering, platform compositions and ShellCheck. Full, Minimal and Chinese USB-without-Reader/Wi-Fi firmware builds passed at 3,027,232 / 563,328 / 666,896 bytes; the existing profile comparison passed with an 81.4% Minimal reduction.
  - The connected ESP32-S3 passed Full flash/boot smoke: 8 MiB PSRAM, partitions, first Launcher frame, boot confirmation, 13 applications and USB CLI. Real USB transfer throughput, cross-OS reopen and sleep/wake interaction remain hardware qualification work; Host round trips verify production framing and storage, not panel or USB timing.

- [x] **E2.1: 动态应用运行时与第三方生态架构预研 (Dynamic Application Runtime & Extensibility Research)**
  - **背景与愿景**：
    - 掌上随身墨水屏终端的长期生命力在于开放的第三方极客应用生态。
    - 对标 GitHub Milestone #6（`Research — Dynamic application runtime`），探索让第三方应用在不重新编译或全量烧录整机固件的前提下，被独立分发、加载与运行的技术路径。
  - **交由 Astra 深度推演的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *执行载体与沙箱隔离*：如何权衡原生精简 ELF 动态重定位（对标 Flipper Zero `.fap`）、轻量 WebAssembly 字节码虚拟机（如 Wasm3/WAMR）以及微型脚本引擎在 ESP32-S3（无 MMU、8MB 八线 PSRAM）上的内存开销、执行性能与故障隔离能力？
    2. *稳定二进制 ABI 与系统调用边界*：如何从当前的 C++17 源码级 SDK（SDK v1）逐步沉淀出一套版本化、二进制稳定的系统调用跳转表（Syscall Jump Table），确保第三方应用在底层固件升级迭代时保持良好的跨版本运行兼容性？
    3. *动态发现、生命周期与存储流转*：独立编译的微应用如何借助 USB/文件系统通道（衔接 E1.8 成果）进行热插拔安装与管理？启动器（Launcher）与场景栈如何动态解析应用元数据并实现零碎片加载与退出清理？
  - **期待产出**：
    - 输出系统性技术预研报告，深入评估各技术路线在 Note4 软硬件平台上的可行性、内存/Flash 预算开销与演进阶段建议。
  - Compared Espressif ELF, Wasm3, WAMR and Lua with CrossPoint workflows and Flipper FAP/SceneManager/ViewPort ownership. Documented the proposed binary host boundary, paged Apps destination, USB/storage installation and bounded lifecycle in [docs/DYNAMIC_APPLICATION_RESEARCH.md](docs/DYNAMIC_APPLICATION_RESEARCH.md).
  - Completed executable probes for memory limits, malformed input, copied imports, 100 load/unload cycles and callback/initialization loops. Corrected Wasm3 budget reset, protected and metered Lua initialization, and retained WAMR's writable module input through unload. Experimental engines remain outside normal firmware and Host builds.
  - Full-image links add 63,952 / 64,032 / 79,792 bytes for Wasm3 / WAMR / Lua; all fit the existing 3 MiB slots. Metered WAMR is the preferred compiled-app prototype, with Lua as a personal-scripting alternative. The report records Wasm initialization timeouts and reproducible Host UBSan findings rather than claiming safe arbitrary-code execution.
  - Verified all 35 production Host targets, the Full firmware build, three isolated ESP32-S3 probe links and restricted Lua ASan/UBSan. SDK v1 stays source-compatible and static; third-party loading and physical guest execution remain subsequent implementation work. No hardware flash, partition change or new release gate was needed.

---

## GitHub Project & Milestone Governance (项目治理与远程同步)

- [x] **G1.1: GitHub Issues 与 Milestones 状态审查与同步治理 (GitHub Issues & Milestones Triage)**
  - 使用 `gh` CLI 检查并分析远程 GitHub 仓库的现有 Issue 与 Milestone 状态。
  - 根据工程实际已交付完成的 Milestone（C1 互联平台、D1 维护命令行、M5 OTA更新、R1 墨水屏进阶、L1 实用启动器与阅读器、S1 模块化解耦等），系统性审查关联的 Issue，更新交付说明并规范关闭已解决的问题。
  - 同步创建或更新 GitHub Milestones（如 L1, S1, E1），对齐远程仓库的项目进度看板，确保 GitHub 状态与本地工程契约保持一致。
  - Reviewed 47 existing Issues and eight Milestones with gh; added delivery/evidence updates to all 16 open Issues. Closed the completed M5 architecture decision #7 and recorded completed R1/Q1/L1/S1 delivery in #59–#62 and their Milestones.
  - Resolved the R1 research/display naming collision, added E1 tracking and updated PR #54 to its actual E1.1–E1.7 scope. Preserved the new E1.8 plan in #58 and retained unfinished C1/D1 implementation and physical acceptance work. Existing OTA and device-measurement follow-ups are explicit in #55–#57.
  - Verified all 34 Host targets and read back GitHub states, assignments and PR metadata. Updated README/roadmap and recorded the scope mapping in [docs/GITHUB_TRIAGE.md](docs/GITHUB_TRIAGE.md). No firmware or hardware change was needed for this governance iteration.

- [x] **E2.2: 动态应用运行时原型落地与微应用试点 (Dynamic Application Runtime Prototype & Micro-App Pilot)**
  - **背景与愿景**：
    - 承接 E2.1 架构预研与基准测量结论（[docs/DYNAMIC_APPLICATION_RESEARCH.md](docs/DYNAMIC_APPLICATION_RESEARCH.md)），将动态应用从“纯理论调研与隔离探针”推向“最小可用工程原型（Minimum Viable Prototype）”。
    - 让 Note4 启动器初步具备发现并载入独立沙箱微应用的能力，使极客用户能在不重新编译/烧录整机固件的前提下探索第三方扩展。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *宿主运行时边界与首选适配器落地*：根据 E2.1 中针对 WAMR（经典解释器、指令配额计量）与受限 Lua 5.4 的优劣权衡，选择最可控且符合当前固件预算的路径构建可选的沙箱适配组件（如 `zectrix_runtime`），解决内存对齐或初始化边界问题。
    2. *极简系统调用接口设计*：为沙箱暴露哪些必要的轻量 Native 宿主功能？如何通过零分配/只读快照将物理按键事件输入、单色 15KB 画布绘制以及退出返回控制以最小摩擦暴露给动态应用？
    3. *端侧动态发现与运行生命周期*：如何在 Launcher 引入分页式“Apps”磁贴或动态入口？如何确保应用异常崩溃、配额耗尽或用户长按退出时，沙箱能够干净回收内存并恢复系统前台状态？
    4. *示范微应用原型验证*：自主设计并实现 1~2 个小巧实用的独立微应用示例（如计算器、简易备忘、卡片复习等），验证端到端闭环。
  - Implemented optional restricted Lua 5.4.9, metered initialization/events/rendering, a 128 KiB allocation budget and copied commands clipped inside the system canvas. Paged Apps discovery and List/Loading/Running/Error scenes preserve system Back/shutdown and reclaim files/VM memory on failure or exit.
  - Added Calculator and Flashcards as independent scripts, plus USB app-list/app-put/app-get/app-remove. AppStorage shares the existing mount, exclusive lease, staging and no-overwrite commit while preserving TXT/EPUB name validation. Documented the pilot API, installation and limits in [docs/MICRO_APPS.md](docs/MICRO_APPS.md); packaging remains E2.3.
  - Verified all 36 Host targets, strict runtime and USB ASan/UBSan, eight USB client/integration scenarios, 100 VM/foreground lifetimes, clipped pilot previews and ShellCheck. Full/Minimal firmware builds passed at 3,112,640 / 563,872 bytes; the profile comparison passed with an 81.9% Minimal reduction. Full retains 33,088 bytes in the existing 3 MiB slot. No hardware flash or partition change was needed.

- [x] **E1.9: 原生随身极客实用工具集演进 (Native Geek Utility Pack)**
  - **背景与愿景**：
    - 汲取 Flipper Zero、Biscuit 等优秀开源固件的实用性设计，进一步挖掘 400x300 双周长续航墨水屏与三键交互作为“日常随身数字挂件/极客终端”的潜力。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *工具选型与使用场景*：在墨水屏与三键限制下，哪些原生离线工具最能体现随身设备的价值？例如专注番茄钟（Pomodoro Timer）、离线万年历与节气卡片（Perpetual Calendar）、极简便签/闪卡（Memo/Flashcards）或其他实用小工具？
    2. *低频常显与功耗哲学*：微工具在前台运行或待机锁屏时，如何合理运用局部刷新与休眠调度，既保持即时信息可读性，又守住低功耗底线？
    3. *模块化裁剪一致性*：新增的原生小工具如何与 `components/zectrix_app`、Kconfig 与 ServiceRegistry 优雅结合，保持极小固件（Minimal Profile）下随时可一键裁剪的纯洁度？
  - Added optional Home > Pocket Tools with a 5–120 minute focus timer and manual five-minute breaks, a browsable 1900–2199 Gregorian calendar with Today/year-month jump, and a bounded tally counter with reset undo. English/Chinese controls and six private scenes preserve global Back/shutdown; [docs/UTILITIES.md](docs/UTILITIES.md) records usage and lifecycle.
  - Reused TimeService, SceneManager and the shared 15,000-byte canvas. Monotonic timing retains exact pause/resume and same-boot state across foreground recreation; visible countdowns update by minute, failed frames retry with Quality, and hidden timers do not redraw unrelated tools. No background task, storage writes, radio work or wake alarm is added; shutdown clears temporary state.
  - Verified all 37 Host targets, utility ASan/UBSan, both language renderers/previews and ShellCheck. Full/Minimal firmware and profile comparison passed at 3,120,016 / 564,896 bytes; Full adds 7,376 bytes and retains 25,712 bytes in the existing slot. Minimal excludes utility sources and session RAM. The simulated minute update transfers 880 bytes; no hardware flash or partition change was needed.

- [x] **D1.4: 维护终端指令集补全、系统级状态反射与多源时钟同步演进 (Maintenance CLI Completeness, System Reflection & Multi-Source Clock Synchronization)**
  - **背景与愿景**：
    - 针对前期治理遗留的维护终端能力缺口（对标 GitHub Issue #44, #45, #46），全面充实 USB Maintenance CLI 的全景状态反射、无损观测与安全防护能力。
    - 解决随身墨水屏终端在离线长待机与异构无线环境下的系统时钟准确性与 PCF8563 硬件 RTC 持久化问题。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *全景状态反射与无阻塞快照 (System Status Reflection - Issue #44)*：
       - 如何通过 ServiceRegistry 安全获取各硬件子系统状态并格式化输出？例如：
         - 电源子系统 (`power status`)：电池 ADC 采样电压、充放电状态引脚、阶梯低电量阈值与待机估算；
         - 射频网络 (`connectivity status`)：2.4GHz Arbiter 仲裁模式、Wi-Fi (STA/AP/Off) SSID/RSSI/IP/MAC、BLE 连接与广播状态；
         - 应用与场景栈 (`app list` / `app current` / `scene dump`)：前台 SceneManager 栈层级、视图拓扑以及动态微应用（WAMR/Lua）资源配额与堆消耗；
       - *并发安全原则*：在终端查询时，如何坚持零等待、只读快照机制，确保不长时间持有模块互斥锁，绝不阻塞墨水屏主刷新循环。
    2. *观测流与无损事件监听 (Observation Streams & Non-consuming Input Tap - Issue #45)*：
       - *物理三键监听*：如何在不截断、不吞噬前台 App 正常事件分发的前提下，实现类似 Linux `evtest` 的轻量非侵入按键镜像 (`input watch`)？
       - *背压与内存防护*：在日志高频突发或主机消费缓慢时，环形缓冲区（RingBuffer）应采用何种策略，确保绝不拖垮 ESP32-S3 堆内存与主系统稳定性？
    3. *确认性状态变更与安全性边界 (Confirmed Mutations & Safety Boundaries - Issue #46)*：
       - 对重启 (`reboot`)、休眠 (`sleep`)、存储抹除 (`storage wipe`) 与出厂重置 (`factory reset`) 等破坏性操作，如何设计确认防线？
       - 权衡终端两阶段交互式确认与“屏幕物理按键确认（Physical Ownership Proof，如屏幕弹出提示由机身实体按键短按放行）”的安全体验与防呆效果。
    4. *射频物理现实下的多源时钟同步与 RTC 硬件防护 (Multi-Source Clock Sync & RTC Hardening)*：
       - *同步范式权衡*：结合 ESP32-S3 射频事实（Wi-Fi Beacon TSF 仅为 AP 相对开机计数而非绝对 UTC，BLE 广播无标准化 UTC），在“低功耗离线优先、杜绝额外射频待机开销”的前提下，权衡主动定时连网与“机会主义/寄生式对时（Opportunistic Piggybacking，在 Web 传书、OTA、微应用 HTTP 访问或伴侣连接时顺带校准）”；
       - *异构时钟源仲裁*：面对 LwIP SNTP、HTTP 响应头 Date、BLE CTS (0x1805)、Companion Hello TLV 与 NFC，如何设计统一的优先级裁决模型与时区/UTC 解耦；
       - *RTC 原子写与跳变平滑*：如何确保 PCF8563 I2C 寄存器写入时进位不出现脏数据？外部时钟偏移时如何防御跳变（Step vs Slew）对前台调度器的冲击？
       - *维护终端指令*：设计 `time status` / `time sync` 等状态探查与手动校准指令。
  - Added `power status`, `connectivity status`, `app list/current`, `scene dump` and `time get/status/sync`, using typed services, cached power/radio data and copied foreground/scene/ViewPort/Lua quota snapshots. Queries do not start ADC sampling, radio work, rendering or guest callbacks; contended radio snapshots return Busy. Pairing and individual bond removal remain physical Connectivity actions.
  - Added non-consuming `input watch` with a 16-record overwrite ring, four-record reads, bounded output and explicit loss counts. Confirmed USB time changes, reboot, sleep, storage wipe and factory reset bind exact arguments to a 15-second challenge; cancellation, expiry and disconnect retire it. Deferred power/reset operations exit the foreground and release file/transfer leases first; unknown outcomes never trigger automatic retries.
  - Unified Manual > authorized Companion > verified HTTPS Date > RTC clock authority, with ten-minute priority holdoff, stale-sample rejection and bounded HTTPS corrections. Existing HTTPS traffic supplies optional validated Date samples without another connection; UTC and offset remain separate, sub-two-second automatic corrections avoid writes, and RTC persistence retains the STOP/calendar/resume protection. Schedulers remain monotonic. See [docs/TIME.md](docs/TIME.md) and [docs/MAINTENANCE_CLI_CONTRACT.md](docs/MAINTENANCE_CLI_CONTRACT.md).
  - Verified all 37 Host targets, 13 CLI PTY scenarios, CLI/Platform/Time ASan/UBSan and ShellCheck. Full/Minimal firmware and profile comparison passed at 3,139,120 / 565,392 bytes; Full adds 19,104 bytes and leaves 6,608 bytes in the existing slot. No hardware flash or partition change was performed; physical USB/power/RTC recovery qualification remains separate.

- [x] **S1.2: 固件空间治理、Flash 分区重构与静态资源解耦推演 (Firmware Budget Governance, Partition Topology & Asset Decoupling)**
  - **背景与愿景**：
    - 实机烧录实测显示，当前 Full 固件已达 3,139,024 字节，距离 3MB 分区硬上限仅剩 6.7KB（0.2% 空间），固件面临空间耗尽风险。
    - 结合板载 16MB 物理 Flash 中尚有约 2.9MB 完全闲置未分配、且固件内嵌字库占据 1.33MB 的客观现实，开展系统性固件预算治理与架构推演。
    - **【架构授权与探索原则】**：我们不预设特定的单一解决方案。**全面授权并鼓励 Astra 自由权衡分区拓扑扩容、静态字库分区外部化、以及工具链级代码瘦身等多种路径**；只要能从根源上消除空间焦虑且保持生产级稳定性，均予以完全信任。
  - **交由 Astra 自由探索与自主决断的核心设问 (Open Architectural Inquiry for Astra to Lead)**：
    1. *分区拓扑重构与 16MB Flash 空间再平衡*：
       - 如何利用末端未分配的 2.9MB 闲置空间？在 A/B OTA 容灾机制（`ota_0`/`ota_1`）与 `factory` 镜像留存之间，如何权衡插槽尺寸（如扩充至 4.0MB）与回滚安全边界？
    2. *大体量静态资产解耦与架构权衡*：
       - 内嵌 1.33MB 点阵字库是固件膨胀的主因。将其剥离为独立 Flash 数据分区 vs 保持固件内嵌并引入轻量压缩，各自在 OTA 传输开销、首刷复杂度及 Minimal Profile 纯洁性上有何利弊？
    3. *工具链优化与死代码压榨*：
       - 评估 LTO 链接时优化、未使用 C++ 虚表/模板裁剪以及 mbedtls/NimBLE 配置微调对纯代码段的实际缩减效果。
    4. *平滑迁移与测试套件自愈*：
       - 确保任何分区变动均能平滑穿透 `partitions.csv`、`tools/build-firmware.sh`、`tools/device-smoke-test.sh` 及 Host 自动化回归，杜绝实机烧录错位。
  - Selected production `-Os` and lossless 8x8 font-tile sharing after measuring compiler, DEFLATE and tile alternatives. The complete 40,181-glyph Unifont subset shrinks from 1,325,973 to 824,959 bytes. Widths and rows read immutable Flash directly with no heap, decode workspace or shared mutable cache; 16px/24px rendering and SDK behavior are preserved.
  - Retained the existing factory/A/B slots, NVS and 4 MiB book store. Evaluated equal `0x3f0000` slots, asymmetric recovery/OTA images, external font banks and LTO in [docs/FIRMWARE_BUDGET.md](docs/FIRMWARE_BUDGET.md). The selected reduction restores useful capacity without a data migration or new asset/rollback dependency.
  - Added per-build `firmware-budget.json` from the generated native partition table, linked font symbol and actual application image. Hardware smoke now matches boot partition addresses/sizes to that report. Current, expanded and asymmetric layout tests remove dependence on a second set of hardcoded offsets; no new size gate or saved baseline is added.
  - Verified all 39 Host targets, Font/Reader ASan/UBSan, every original glyph pixel, upstream BDF regeneration, English/Chinese renderers, actual Full-image Host OTA streaming and ShellCheck. Full/Minimal builds and profile comparison passed at 2,482,208 / 514,528 bytes. Full saves 656,912 bytes and leaves 663,520 bytes (21.1%) in each existing slot; static internal RAM is 201,815 / 108,315 bytes. No hardware flash was needed.

- [x] **R1.3: 算法点阵排版样式引擎与墨水屏富文本渲染 (Algorithmic Typography Engine & Rich Text Rendering)**
  - **背景与愿景**：
    - 当前系统（UI、Reader、Micro-Apps）仅支持单一常规体（Regular）点阵渲染，缺乏粗体、斜体与层级样式表现力。
    - 坚持“零 Flash 膨胀、零动态堆分配”的嵌入式哲学，不额外引入膨胀的多字重字库，探索纯算法实时点阵合成（Algorithmic Styling）的排版演进路径。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *样式契约与 API 架构*：
       - 如何在 Canvas 绘制层与 SDK 接口中优雅引入无开销的样式抽象（如 `TextStyle` 紧凑位掩码：Bold、Italic、Dim、Underline、Keycap 等）？
       - 评估在 Reader 或文本展示层支持轻量 Markdown / 富文本行内标记的解析可行性与边界。
    2. *几何变换与度量联动 (Metrics & Reflow Consistency)*：
       - 算法形变（如横向加粗 +1px、剪切倾斜 Slant）如何与 `TextWidth()`、`GlyphWidth()` 及 Reader 分页排版引擎协同，杜绝度量偏差引起的文本截断与排版错位？
    3. *中西文非对称表现与容错降级 (CJK vs ASCII Asymmetry)*：
       - 针对 16px/24px 汉字笔画密集易粘连的物理现实，如何权衡中文字符的粗体膨胀量与斜体倾斜度？是否存在最适合低分辨率汉字的点阵防粘连规则？
    4. *墨水屏物理友好型样式落地*：
       - 权衡并实现 1~2 种高实用性墨水屏专属样式：如利用 Bayer 网点掩码模拟次要文字（免灰阶刷新延迟）、大标题空心字（防微胶囊过度翻转与残影）、实体按键键帽提示框（`[OK]`）等。
  - Added SDK 1.2's one-byte TextStyle vocabulary, shared measurement/raster geometry, Latin dilation/shear and CJK underline fallback. Canvas fitting, centering, inversion and clipping account for styled extents; Bayer secondary ink and whole-run keycaps use the existing 1bpp surface without new font assets or rendering allocations.
  - Preserved streamed EPUB headings, bold, emphasis, underline and secondary text with sixteen bounded style entries. Page glyphs remain eight bytes; bottom-line decoration, backward turns, chapter replay and font reflow preserve source-byte anchors. TXT remains literal. See [docs/TYPOGRAPHY.md](docs/TYPOGRAPHY.md) for markup boundaries and measured costs.
  - Applied styles to system headings, Home and both Lua pilots; optional copied style flags retain old script behavior and existing quotas. Guest drawing now intersects the caller's viewport clip, including keycap borders and glyph expansion.
  - Verified all 39 Host targets, Reader/Localization/Runtime ASan/UBSan, English/Chinese previews and Full/Minimal firmware/profile comparison. Full/Minimal are 2,486,256 / 516,528 bytes (+4,048 / +2,000); font assets and static internal RAM are unchanged. Full retains 659,472 bytes (21.0%) per slot. No hardware flash was needed.

- [x] **R1.4: 墨水屏物理特性观测、分析建模底座与自适应调度 (Display Physics Telemetry, Analytical Modeling Foundation & Adaptive Scheduling)**
  - **背景与愿景**：
    - 当前系统对墨水屏刷新的调度基于经验静态计数，缺乏对物理状态（温度、电池跌落、翻转像素密度、忙闲周期）的结构化观测与数学代价建模。
    - 在外部精密仪器标定前，优先在代码层面筑牢“观测基建（Telemetry Infrastructure）”与“参数化模型抽象”，为未来的功耗优化与显示质量闭环奠定坚实底座。
    - **【架构授权与探索原则】**：本任务列举的能耗项与残影债务公式仅作为概念启发与讨论起点。**我们明确鼓励并全面授权 Astra 自由发挥，提出您认为更合理、更严密、或更具工程美感的数学模型**；无论采用何种建模范式，只要推导自洽、在嵌入式算力与内存上可控，我们均予以完全信任并直接通过。
  - **交由 Astra 自由探索与自主决断的核心设问 (Open Architectural Inquiry for Astra to Lead)**：
    1. *零开销物理遥测基建 (Zero-Allocation Telemetry Recorder)*：
       - 如何在驱动与显示服务层设计轻量、无锁的环形帧快照（记录 $T, V_{\text{bat}}, N_{\text{flip}}, W, H, S_{\text{bytes}}, t_{\text{busy}}$），确保监控本身对渲染流水线零性能扰动、零动态堆分配？
    2. *数学建模的深度推演：您是否有更优的物理模型？(Superior Mathematical Modeling Paradigms)*：
       - *残影微观本质*：简单的像素翻转累加（$\sum N_{\text{flip}}$）是否足够？您是否认为引入**空间局部性集聚（Spatial Clustering / Hotspots）**、**高对比边缘电场畸变**、或是**微胶囊双电层极化电荷记忆积分**能更准确地预测残影与底色发灰？
       - *多阶段 LUT 与状态转移*：针对 SSD2683 的多阶段波形脉冲与 2bpp 转换特性，是否存在更具数学美感的状态机表征（如马尔可夫转移矩阵或等效 RC 网络能耗模型）？
       - *低开销数值计算*：在 ESP32-S3 上，如何运用定点数（Fixed-Point Q8.8/Q16.16）或微型查表实现微秒级的在线代价评估，避免高开销浮点运算？
    3. *参数解耦与未来标定注入 (Parametric Decoupling & Calibration Hook)*：
       - 如何抽象出通用的参数字典（基础静态项、翻转动态项、温度阿伦尼乌斯因子、电荷自发弛豫衰减率），使得未来一旦有仪器实测回归数据，只需更新系数即可无缝收敛？
    4. *模型驱动的自适应刷新调度 (Model-Driven Adaptive Scheduling)*：
       - 如何用动态评估的“残影债务预算（Ghosting Debt Budget）”彻底取代死板的换页计数器，在常温、低温以及不同阅读排版场景下实现智能自适应全刷？
    5. *仿真验证与离线数据导出工具链 (Host Simulation & Telemetry Tooling)*：
       - 如何在 Host 自动化测试中模拟高频切换与长周期阅读，验证您设计的观测与债务模型的收敛性？
       - 如何在维护终端或主机工具中暴露遥测导出接口，为后续真实物理测量与 Jupyter/Python 曲线拟合做好准备？
  - Added twenty-tile directional transition analysis and Q16.16 spatial debt with concentration, signed memory, temperature/supply gains and calibration injection. Replaced fixed frame/cumulative-pixel scheduling while preserving high-contrast cleanup, grayscale preclear and full recovery after failure.
  - Added an allocation-free 16-frame recorder (1,800 bytes on ESP32-S3), actual SPI/RAM/BUSY/phase observations and aged temperature/battery samples. Successful completion commits predicted debt; errors preserve prior debt with an invalid image baseline. Energy estimates require explicit calibration.
  - Added owner-dispatched `display telemetry` / `display model`, typed CSV capture conversion and 24,576-update Host simulations. Small status changes remain partial beyond eight frames; concentrated, dense and cold updates clean sooner. See [docs/DISPLAY_PHYSICS.md](docs/DISPLAY_PHYSICS.md).
  - Verified all 39 Host targets, focused display/model ASan/UBSan, ShellCheck, Full/Minimal builds and profile comparison, connected-device Full flash/boot and real telemetry-to-CSV export. Firmware is 2,492,240 / 520,096 bytes; static internal RAM is unchanged. Optical ghosting, transient voltage sag and energy calibration remain instrument measurements.

- [x] **E1.10: 状态栏微型图标系统与微观视觉重塑 (Status Bar Micro-Icon System: Bluetooth, Wi-Fi & Multi-State Battery Polish)**
  - **背景与愿景**：
    - 随身墨水屏终端在 24px 高度的常显顶部状态栏中，当前蓝牙、Wi-Fi 与电池主要采用初级线段绘制结合文本状态字（如“开/关/连”）标记，不仅占据较多横向空间，且在 1-bit 单色低分辨率墨水屏上缺乏精致度与微观视觉层次。
    - 借鉴经典掌上终端（如 Flipper Zero、Kindle、Pebble、CrossPoint 等）在单色微型点阵下的图形设计造诣，全面重塑状态栏的微型图标系统。
    - **【架构授权与探索原则】**：我们不预设任何死板固化的像素模板或硬编码图案。**全面授权并鼓励 Astra 自由探索最符合 1-bit 墨水屏物理特性的微型点阵表达范式**；在极致空间利用率、直观辨识度、美学韵律与极低 ROM/内存开销之间寻找最佳平衡，提出您认为最优雅的实现。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *电池图标状态表征与微观几何 (Multi-State Battery Dot-Matrix)*：
       - 如何在微型点阵空间（如高 8~11px、宽 14~20px）内清晰刻画电池轮廓与正极端子，杜绝墨水屏低分辨率下的笔画粘连？
       - 阶梯电量表达：如何权衡分段格数（如 4~5 段阶梯式或微积分格位）、低电量镂空/警戒标记？
       - 充放电与异常状态：当设备处于 USB 充电、充满、外部供电或充电故障时，如何在不破坏电量可读性的前提下，通过内嵌或伴随的微型符号（如闪电、插头、加号、感叹号）实现一目了然且富有张力的视觉区分？
    2. *无线射频状态统一与紧凑对称 (BLE & Wi-Fi Micro-Indicators)*：
       - 纯图形自解释性：如何摆脱多语言文本标签（"开/关/连"），仅凭纯图形微标在紧凑点阵下自解释多种射频状态（关闭/未使能、广播/扫描中、已连接、数据收发活跃、故障/错误）？
       - 视觉对称与节奏：BLE 的卢恩符文（Nordic Rune）与 Wi-Fi 的阶梯/扇形信号弧在尺寸、线条粗细和留白上如何达到微观视觉平衡，避免单侧视觉失重？
    3. *24px 状态栏紧凑排版、对齐与微观栅格 (Vertical Alignment & Spacing Grid)*：
       - 状态栏内各元素（左侧/中间时钟文字结合 R1.3 样式、右侧射频微标、电池图标与百分比文字）如何建立统一的垂直居中基线与横向留白步长？
       - 反色与高亮场景适应：当状态栏处于反色高亮或特定背景层叠时，微型图标的 1-bit 边缘对比度与可读性表现。
    4. *零堆分配与极简内联表征 (Zero-Allocation & ROM Budget)*：
       - 探索微型图标的最佳存储与绘制范式：是采用紧凑的 `constexpr` 静态位图数组（Bitmap/Tile），还是高度参数化的微型几何图元绘制？
       - 确保全套图标系统对 ROM/Flash 增加控制在数十至数百字节以内，绘制流水线零动态堆分配、零浮点开销，保持对 Minimal Profile 极限裁剪的绝对友好。
    5. *自动化验证与微观视觉回归 (Visual Previews & Headless Verification)*：
       - 如何在 Host 测试套件中生成状态栏在各种电量百分比（0%, 5%, 20%, 50%, 80%, 100%）、充电状态与射频组合下的 ASCII/点阵预览快照，确保任何改动均有清晰的回归基线与可追溯性？
  - Replaced radio text with original five-state BLE/Wi-Fi graphics and a 20 x 10 five-cell battery. Charger-full, charging, external power, low/fault and unknown/absent states retain readable charge levels. The 198-byte masks support clipped inverse rendering with no drawing allocation or floating-point work.
  - Wired existing power flags and sampled radio work through the foreground adapter. Idle book servers show no transfer activity; hidden or clamped values do not invalidate the status viewport. Existing scene ownership, gray-image recovery and static sleep covers remain intact.
  - Added seventeen normal/inverse pixel and ASCII preview cases, state-source integration and three-profile allocation/clipping checks. Verified all 39 Host targets, display/localization ASan/UBSan, Full/Minimal builds/profile comparison and connected ESP32-S3 Full flash/boot. See [docs/STATUS_BAR.md](docs/STATUS_BAR.md).
  - Full/Minimal firmware is 2,492,576 / 520,304 bytes (+336 / +208); static internal RAM is unchanged / +8 bytes. Tested minute changes still transfer 44 native RAM bytes; a radio activity mark change transfers 14 bytes without changing content pixels.

- [x] **D1.5: 系统故障注入、容灾自愈与长周期浸润可靠性 (Fault Injection, Self-Healing & Health Supervisor)**
  - **背景与愿景**：
    - 随身墨水屏设备面临异常掉电、外部射频突发干扰、NVS 损坏等复杂边缘场景，系统需具备工业级自诊断与自愈韧性（对标 GitHub Issue #56, #57）。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *异常与断电模拟*：如何设计文件系统断电写坏、NVS 校验和失效、无线射频连续重试超时等故障注入测试？系统如何自主触发安全回滚或状态重置？
    2. *硬件看门狗与死锁防御*：如何确保当第三方应用或前台场景发生长时间挂起时，硬件与软件看门狗能够平滑保护底座系统？
    3. *长周期稳定性浸润（Soak Test）*：设计多场景长周期高频轮转测试，验证整机在无内存泄漏、无句柄耗尽前提下的长期运行可靠性。
  - Added foreground-owned health supervision with a 90-second RTC watchdog, preserved trial-boot confirmation deadlines and cleanup protection, and exposed copied observations through read-only `system health`. Polling and input waits cannot feed a hung foreground.
  - Three consecutive application errors release the failed app before reopening Home. A failing Home retains bilingual recovery, OK/Back retry, long-DOWN shutdown and USB maintenance on the shared canvas. Panic/watchdog boots suppress radios and automatic showcase; SDK faults suppress automatic showcase for the current boot.
  - Preserved NVS on initialization/open errors while keeping independent files and local maintenance usable. Upload handles reject retries after uncertain I/O; Wi-Fi cleanup retries transient faults within its existing two-second window while retaining ownership. See [docs/RELIABILITY.md](docs/RELIABILITY.md).
  - Verified all 40 Host targets, focused ASan/UBSan, 4,096 application recovery cycles, 1,024 Wi-Fi and Lua fault/restart cycles, 120 HTTP cancellations, 256 recovery/sleep UI cycles per language and seven simulated days of health progress. Corrected packed-font subset generation and checked recovery previews in English/Chinese.
  - Full/Minimal builds and profile comparison passed at 2,495,312 / 521,648 bytes (+2,736 / +1,344); static internal RAM is unchanged. Connected ESP32-S3 Full flash/boot and 95.15 seconds of continuous watchdog feeding passed. Physical interrupted OTA and separate endurance/power measurements remain tracked by #56/#57.

- [ ] **G1.2: 远端代码同步与 GitHub 状态治理闭环 (Remote Git Sync & GitHub Governance)**
  - **背景与愿景**：
    - 本地主线已积累了大量高质量原子 Commit（含 E1.8、E2.1 及后续成果），需要将这一阶段性重大成果与远端 GitHub 保持同步，并治理关联的 GitHub Issues。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *远端分支对齐与 PR 治理*：检查本地与远端分支状态，审查 PR #54 或更新相关 PR，确保提交历史干净规范；
    2. *Issue 状态同步*：使用 `gh` CLI 审查并更新远端 GitHub Issue（特别是已由 E1.8 完整解决的 Issue #58 与关联 Milestone 13），更新验证记录；
    3. *阶段性版本固件打包准备*：准备 v1.2 阶段性 Release 资产（如 Full / Minimal 固件二进制与校验哈希）。

- [ ] **E2.3: 极客应用分发与打包工具链探索 (`.zapp` Package Specification & CLI Toolchain)**
  - **背景与愿景**：
    - 在 E2.2 动态沙箱原型落地后，为第三方开发者提供友好的外部构建与单文件打包规范（对标 Flipper Zero `.fap` 与 Android `.apk` 思想）。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *容器格式与元数据设计*：设计轻量自包含格式（如 `.zapp`），如何将 16x16 / 32x32 单色图标、应用名称、版本号、作者、权限声明、指令配额（Quota）与编译字节码打包为单一可交换二进制文件？
    2. *极客命令行构建工具*：开发极简的打包与校验工具（使用 `bun` 或 `python`），支持开发者一键执行 `zapp build` 与 `zapp pack`；
    3. *USB / Wi-Fi 传包协同*：与现有的 USB Manager 及 Web 传书通道无缝衔接，实现通过浏览器或命令行拖拽安装第三方应用。

- [ ] **C2.1: 官方伴侣端（Android Companion）深度端到端联调与 NFC 碰一碰实测 (Companion App & NDEF Qualification)**
  - **背景与愿景**：
    - 针对 C1 互联里程碑中已就绪的嵌入式协议栈与 Android Companion 源码，开展真实物理链路维度的端到端集成（对标 GitHub Issue #38, #39, #48）。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *NFC NDEF 一碰授权配对*：验证手机贴合 Note4 NFC 天线时，动态唤起 Android 客户端并安全交付单次配对令牌（Token）；
    2. *双向持久化数据同步*：在真实 BLE 连接下，验证阅读进度流式回传、离线排队重发、手机端天气/时间校准同步；
    3. *手机端传书与画报推送*：通过手机端伴侣应用一键推送电子书或待机画报至 Note4 存储分区。

- [ ] **R2.1: Note4 生产级固件全量发布与用户使用手册 (Production Firmware Release Pipeline & User Handbook)**
  - **背景与愿景**：
    - 随着各项软硬件特性的全面成熟，Note4 平台具备了发布正式 Release 生产版本固件的条件。
  - **交由 Astra 自由探索与权衡的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *多架构固件构建矩阵*：自动化输出 Full（全功能版）、Minimal（极限离线版）及特定微调版的最终 Release 固件包与 SHA256 校验列表；
    2. *中英双语图文手册*：产出系统化、对极客与普通用户兼顾的《Note4 快速上手与极客指南》（含按键操作流、USB/Wi-Fi 传书教程、动态应用安装指南）；
    3. *发布说明与版本里程碑收敛*：起草完整的 GitHub Release Note，收敛并关闭对应里程碑。
