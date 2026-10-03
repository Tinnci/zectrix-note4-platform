# Documentation / 文档导航

先选任务，再读专题。README 展示产品，手册讲操作，接口文档说明实现边界。
Historical records are evidence for their recorded revision, not current setup instructions.

## Start here

| 任务 / Task | 入口 / Read |
| --- | --- |
| 使用设备、按键、传书 | [中文手册](HANDBOOK_zh.md) · [English handbook](HANDBOOK.md) |
| 源码构建、首次烧录 | [Quick start](QUICK_START.md) · [环境要求](PREREQUISITES.md) |
| 从旧版升级 | [Note4 SDK v2 迁移](NOTE4_MIGRATION.md) |
| 选择 Full / Reader / Minimal | [模块化构建](MODULAR_BUILD.md) |
| 运行测试、定位失败 | [CI 与主机测试](CI.md) |
| 生成界面预览 | [UI previews](UI_PREVIEW.md) |
| 开发应用 | [SDK v2](SDK_V2.md) · [Lua apps](MICRO_APPS.md) · [.zapp 包](ZAPP_PACKAGES.md) |
| 配置连接和后台刷新 | [连接配置](CONNECTIVITY_CONFIGURATION.md) · [HA bridge](../tools/ha-bridge/README.md) · [Android](../android-companion/README.md) |
| 生成、验证和发布固件包 | [Releasing](RELEASING.md) |

Source version **2.0.0** is not a published-release promise.
[Development notes](releases/v2.0.0.md). Back up data and check hardware/layout
before flashing; do not erase NVS or initialize books for a naming migration.

## Features and UI

- [Home](HOME.md) · [navigation](NAVIGATION.md) · [UI flow](UI_FLOW.md)
- [Reader](READER.md) · [Wi-Fi transfer](BOOK_TRANSFER.md) · [USB host tools](USB_HOST.md)
- [Sleep covers](SLEEP_COVER.md) · [daily lines](SLEEP_QUOTES.md) · [time and RTC](TIME.md)
- [Screen direction](SCREEN_DIRECTION.md) · [status bar](STATUS_BAR.md) · [Pocket Tools](UTILITIES.md)
- [Typography](TYPOGRAPHY.md) · [localization](LOCALIZATION.md) · [font optimization](FONT_OPTIMIZATION.md)

## Architecture and interfaces

- [Ownership and dependency direction](ARCHITECTURE.md) · [service registry](SERVICE_REGISTRY.md)
- [Display layers](DISPLAY_ARCHITECTURE.md) · [EPD API](EPD_API.md) · [hardware map](HARDWARE.md)
- [Refresh scheduling](DISPLAY_RESPONSIVENESS.md) · [display physics](DISPLAY_PHYSICS.md)
- [Connectivity protocol](CONNECTIVITY_CONTRACT.md) · [radio arbitration](RADIO_ARBITER.md)
- [Maintenance CLI](MAINTENANCE_CLI_CONTRACT.md) · [application lifecycle](M3_APPLICATION_CONTRACT.md)
- [Reliability](RELIABILITY.md) · [firmware capacity](FIRMWARE_BUDGET.md)
- [Platform migration principles](PLATFORM_MIGRATION_PRINCIPLES.md)
- Architecture decisions: [runtime / SDK](adr/0003-freertos-runtime-sdk-boundary.md),
  [connectivity](adr/0004-connectivity-platform.md), [A/B OTA](adr/0005-ab-ota-boot-confirmation.md)

## Development policy

- [Contributing](../CONTRIBUTING.md) · [security reports](../SECURITY.md)
- [Toolchain policy](TOOLCHAIN_POLICY.md) · [ESP-IDF submodule inventory](ESP_IDF_SUBMODULE_INVENTORY.md)
- [Plain technical English](CONTROLLED_TECHNICAL_ENGLISH.md) · [documentation review](ASD_STE100_REVIEW.md)
- [Roadmap](ROADMAP.md) · [issue / milestone mapping](GITHUB_TRIAGE.md)
- Research: [firmware UI](FIRMWARE_UI_STUDY.md), [dynamic applications](DYNAMIC_APPLICATION_RESEARCH.md),
  [date-digit design](design/date-digits/README.md)

## Qualification and history

Software implementation, host simulation and physical qualification are separate.
Old test counts, measurements and completion marks apply to the recorded source
and hardware, not every later commit.

- [Test criteria](TEST_CRITERIA.md) · [qualification reports](qualification/)
- [Original baseline manifest](U0_BASELINE_MANIFEST.md)
- M2 records: [contract](M2_PLATFORM_CONTRACT.md), [composition](M2_PLATFORM_COMPOSITION.md),
  [storage](M2_STORAGE_BASELINE.md), [system](M2_SYSTEM_BASELINE.md), [time](M2_TIME_BASELINE.md)
- [M3 exit evidence](M3_EXIT_GATE.md) · [M4 exit evidence](M4_EXIT_GATE.md)
- [Historical task log](history/ASTRA_TASKS.md) · [v1.2 preview preparation](RELEASE_PREPARATION.md)
- [Release notes](releases/) · [upstream provenance](../UPSTREAM.md) · [third-party licenses](../THIRD_PARTY_NOTICES.md)

When changing behavior, update its topic page. Keep introductory commands in
the quick start, test scheduling in CI and publication commands in Releasing;
link to them instead of copying another full procedure into README.
