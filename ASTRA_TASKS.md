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


- [ ] **E1.8: USB 宿主通信与设备管理架构演进 (Host-Device USB Communication & Management Architecture)**
  - **背景与核心愿景**：
    - 当前 Note4 的 USB 接口局限于只读维护终端，尚未形成完整的数据导入导出与设备控制闭环。
    - 演进愿景是将 USB 接口提升为兼顾“极客交互控制”与“便捷数据流转”的统一宿主通信桥梁（Unified Host Bridge），让日常图书传输、文件管理与系统配置触手可及。
  - **交由 Astra 深度推演的开放性核心命题 (Open Architectural Questions for Astra to Explore)**：
    1. *宿主接入与传输范式*：如何权衡块设备物理磁盘挂载（如 MSC/虚拟 U 盘）、对象级协议（如 MTP）与串行会话流协议（如 CDC-ACM 双模/WebSerial）在跨平台（Mac/Win/Linux/Android）免驱可用性、Flash 文件系统并发安全性与单片机资源开销之间的深层关系？
    2. *统一会话与模式流转*：如何在单一物理连接下，自然融合面向人类的字符交互（命令行设置、状态探查、文件浏览）与面向主机的批量数据流吞吐（文件分块推拉、校验）？是否存在低开销、低摩擦的会话协商与流式切换机制？
    3. *端侧视觉与人机交互协同*：当 USB 正在发生数据交换或配置变更时，墨水屏视觉层与三键状态机应如何优雅配合（例如专属传输看板 vs 呼吸式轻量反馈）？如何在各种链路状态下确保物理按键操控的确定性与流畅感？
  - **期待产出**：
    - 赋能 Astra 结合开源优秀实践（Flipper Zero, CrossPoint, Android ADB 等）与 Note4 既有架构基础设施，深入权衡并提出最符合掌上随身墨水屏特质的最佳实践工程方案。

---

## GitHub Project & Milestone Governance (项目治理与远程同步)

- [x] **G1.1: GitHub Issues 与 Milestones 状态审查与同步治理 (GitHub Issues & Milestones Triage)**
  - 使用 `gh` CLI 检查并分析远程 GitHub 仓库的现有 Issue 与 Milestone 状态。
  - 根据工程实际已交付完成的 Milestone（C1 互联平台、D1 维护命令行、M5 OTA更新、R1 墨水屏进阶、L1 实用启动器与阅读器、S1 模块化解耦等），系统性审查关联的 Issue，更新交付说明并规范关闭已解决的问题。
  - 同步创建或更新 GitHub Milestones（如 L1, S1, E1），对齐远程仓库的项目进度看板，确保 GitHub 状态与本地工程契约保持一致。
  - Reviewed 47 existing Issues and eight Milestones with gh; added delivery/evidence updates to all 16 open Issues. Closed the completed M5 architecture decision #7 and recorded completed R1/Q1/L1/S1 delivery in #59–#62 and their Milestones.
  - Resolved the R1 research/display naming collision, added E1 tracking and updated PR #54 to its actual E1.1–E1.7 scope. Preserved the new E1.8 plan in #58 and retained unfinished C1/D1 implementation and physical acceptance work. Existing OTA and device-measurement follow-ups are explicit in #55–#57.
  - Verified all 34 Host targets and read back GitHub states, assignments and PR metadata. Updated README/roadmap and recorded the scope mapping in [docs/GITHUB_TRIAGE.md](docs/GITHUB_TRIAGE.md). No firmware or hardware change was needed for this governance iteration.
