# Selectable firmware modules

S1.2 adds native `Kconfig.projbuild` options and conditional component/source
dependencies. All public module options default to enabled, preserving the
existing full product. Use `idf.py menuconfig` -> **Zectrix modules**, or supply
an `sdkconfig.defaults` overlay for a separate build.

| Option (`CONFIG_` prefix) | Dependency | Effect when disabled |
| --- | --- | --- |
| `ZECTRIX_ENABLE_CONNECTIVITY` | None | Removes Connectivity, Companion and NFC enrollment service components, BLE tasks and the Connectivity scene |
| `ZECTRIX_ENABLE_WIFI` | Connectivity | Removes the shared AP/STA/scan driver; RF diagnostics report SKIP |
| `ZECTRIX_ENABLE_WIFI_HTTP` | Wi-Fi | Removes the direct HTTPS client and its TLS transport; phone resource requests remain available |
| `ZECTRIX_ENABLE_BOOK_TRANSFER` | Wi-Fi | Removes the HTTP server, embedded Web page and SEND BOOKS scenes |
| `ZECTRIX_ENABLE_READER` | None | Removes TXT/EPUB parsing, pagination, reader scenes and the CJK font |
| `ZECTRIX_ENABLE_USB_CLI` | None | Removes the maintenance component, executor and USB session task; normal ESP-IDF console logs remain |
| `ZECTRIX_ENABLE_UPDATE` | None | Removes streamed firmware writing and commit; core boot protection remains |

`ZECTRIX_ENABLE_BOOK_STORAGE` is derived automatically from Reader or Web book
transfer. When both are disabled, SPIFFS and the book storage implementation
leave the build and no library mount is attempted. The partition table and
stored books remain intact. The explicit `books-flash` target is available
when book storage is enabled; normal firmware flash still preserves books.

Disabling a parent disables its dependent options, even if an overlay requests
them. Web book transfer does not depend on the reader or the HTTPS client.
BLE pairing, durable sync and phone resource requests work without direct
Wi-Fi. Keep the existing NimBLE peripheral and HTTPS validation settings from
`sdkconfig.defaults` when using those modules.

## Build an offline reader

Activate the qualified ESP-IDF environment, then build with a separate config
so existing developer settings are preserved:

```bash
source tools/activate-dev-env.sh
cat > /tmp/note4-offline.defaults <<'EOF'
CONFIG_ZECTRIX_ENABLE_CONNECTIVITY=n
CONFIG_ZECTRIX_ENABLE_READER=y
CONFIG_ZECTRIX_ENABLE_USB_CLI=n
CONFIG_ZECTRIX_ENABLE_UPDATE=n
EOF
idf.py --ccache -B build-offline \
  -D "SDKCONFIG=$PWD/build-offline/sdkconfig" \
  -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;/tmp/note4-offline.defaults" build
```

Set Reader to `n` as well for the core Launcher, clock, settings, diagnostics,
gallery and sleep covers. Set Connectivity to `y` and Wi-Fi to `n` for a BLE
companion build. To configure an existing separate build interactively, retain
its build path: `idf.py -B build-offline menuconfig`.

Defaults apply before the saved `sdkconfig`; editing defaults does not override
an existing saved setting. Use menuconfig for that setting, or a fresh config
path. Target-specific defaults such as `sdkconfig.defaults.esp32s3` keep their
normal ESP-IDF precedence. Empty defaults assignments (`CONFIG_OPTION=`) also
disable boolean options, matching native ESP-IDF configuration.

## Dependency resolution

ESP-IDF 5.5 expands component `REQUIRES` before generating `sdkconfig.cmake`.
Using configuration variables only in the ordinary component registration pass
would miss dependencies on the first build and retain old ones after changes.

`cmake/configure_features.cmake` therefore runs the IDF environment's native
Kconfig library before `project()`. It reads the same component definitions,
defaults, target defaults and saved config, and writes boolean selections into
the build directory. `cmake/zectrix_features.cmake` imports those selections
only during early dependency expansion. Normal registration and compilation
use IDF's generated configuration. The resolver does not rewrite `sdkconfig`.
CMake tracks the configuration inputs so changes rebuild the dependency tree.

`main/Kconfig.projbuild` sources the four component option files even when their
components are absent. Enabled optional components override their automatic
projbuild discovery with `cmake/Kconfig.empty`, avoiding duplicate menus.
This keeps disabled modules available to menuconfig without registering their
runtime libraries. `main`, Platform, App, UI, Storage and Diagnostics each
declare only their selected dependencies and sources.

IDF's Bluetooth component unconditionally requires `esp_wifi` for shared SDK
interfaces. That SDK component can still appear in a BLE-only build graph;
the Zectrix Wi-Fi driver is absent, and `libesp_wifi.a`, `libnet80211.a` and
`libpp.a` contribute no bytes to its linked image. BLE retains shared RF
coexistence support. Disabling Connectivity removes the entire BT/Wi-Fi branch.

## Runtime behavior and boot safety

Platform registers eight core providers and up to three optional providers:
Connectivity, USB maintenance and the firmware writer. Typed lookup returns
null for an excluded provider. The Launcher uses the selected item table,
and optional application classes, controllers and renderers are excluded.
Transfer completion returns home when the reader is absent.
With Wi-Fi disabled, RF diagnostics retain SKIP through the summary and do
not count the omitted test as a failure or an executed test.

Offline reading saves the same NVS bookmark format. Its latest revision stays
pending, so a later connected firmware can enqueue it without losing reading
progress. Calendar and sleep-cover snapshots contain only copied display data;
they do not require reader types. The core UI uses the compact built-in font
when the CJK font is absent, with `?` for unsupported Unicode label characters.

These boundaries preserve the existing CrossPoint-inspired streamed reader,
Web library and static sleep cover while allowing each feature to be selected
independently. Flipper-inspired SceneManager and ViewPort ownership remain in
the core shell, with deferred navigation and one display owner. Reference
comparisons are recorded in [UI_FLOW.md](UI_FLOW.md#reference-designs-and-continuation).

`zectrix_system` owns the mandatory `BootGuard` policy and ESP boot backend.
When the writer is enabled, `UpdateService` delegates boot operations to the
same guard; when it is disabled, Platform owns a standalone guard. In both
cases boot validation and the inherited RTC watchdog run before board setup.
`Platform::Boot().ConfirmBoot()` runs only after startup and the first Launcher
frame succeed. Failed startup and service teardown never disarm an unconfirmed
trial watchdog. Native partition, rollback, image validation and shutdown
safety remain in place. See [ADR-0005](adr/0005-ab-ota-boot-confirmation.md).

This iteration adds the source guards needed for usable selected builds.
S1.3 continues the broader application composition and persistent RTC work;
S1.4 covers a reusable minimal-profile firmware/device regression workflow.

## Verification

```bash
bash tools/test-module-config.sh
bash tools/test-platform.sh
bash tools/test-reader.sh
bash tools/test-host.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh
```

Module tests use `uv` to provision the same `esp-idf-kconfig` 2.5.4 library as
the qualified IDF environment. They exercise a fresh full config, contradictory
parent/child requests, independent Web/reader/HTTPS choices, empty defaults,
defaults versus saved settings, and repeated off/on/off changes. The resolver
leaves input files intact. Launcher tests navigate full, core, offline, Web
and BLE menus, checking omitted destinations, selection restoration and wrap.
Platform tests compile the production composition with optional dependencies
absent, individually present and fully enabled, including trial-boot failures
and shutdown. Reader tests run with and without connectivity sources and
exercise offline persistence and subsequent durable synchronization.

The S1.2 ESP32-S3 builds used the same partition layout and SDK defaults:

| Build | Selected optional modules | Application binary | Reduction from Full |
| --- | --- | ---: | ---: |
| Full | All | 2,990,112 bytes | — |
| Core | None | 545,216 bytes | 81.8% |
| Offline | Reader | 1,953,232 bytes | 34.7% |
| BLE | Connectivity, USB CLI; Wi-Fi off | 913,888 bytes | 69.4% |
| Web | Connectivity, Wi-Fi, Web transfer, Update | 1,429,296 bytes | 52.2% |
| HTTPS | Connectivity, Wi-Fi, HTTPS, Update | 1,523,200 bytes | 49.1% |

`project_description.json`, `compile_commands.json` and linker archive sizes
confirm selected components/sources and the absence of disabled module code.
Core `.dram0.data` plus `.dram0.bss` totals 31,772 bytes versus 71,776 in Full,
including section alignment. These are static linker allocations, not runtime
heap or power measurements.
These measurements describe this iteration, not a fixed size requirement.

All 32 Host targets, the six firmware builds and ShellCheck passed. A fresh
core build also passed. Switching its saved Reader setting off/on/off rebuilt
the dependency tree each time, adding and removing SPIFFS, reader sources and
font data together. The connected ESP32-S3 full-firmware smoke test verified
flash, PSRAM, the partition table, platform startup and the Launcher runtime.
The RTC voltage-low condition exercised the existing fallback; persistent RTC
recovery remains S1.3. Selected-profile device interaction and standby current
remain hardware qualification work.
