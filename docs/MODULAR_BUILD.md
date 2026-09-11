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
| `ZECTRIX_ENABLE_READER` | None | Removes TXT/EPUB parsing, pagination, reader scenes and the broad CJK reader font |
| `ZECTRIX_ENABLE_RUNTIME` | None | Removes Lua, the Apps destination and micro-app USB operations; see [Micro-apps](MICRO_APPS.md) |
| `ZECTRIX_ENABLE_UTILITIES` | Core TimeService only | Removes Pocket Tools, its timer/calendar/counter controllers, renderer and RAM session; see [Pocket tools](UTILITIES.md) |
| `ZECTRIX_ENABLE_UI_CHINESE` | None | Removes the Chinese UI strings and its small font subset when Reader is also off |
| `ZECTRIX_ENABLE_USB_CLI` | None | Removes the maintenance component, executor and USB session task; normal ESP-IDF console logs remain |
| `ZECTRIX_ENABLE_USB_HOST` | USB CLI | Removes the binary book/settings session, USB Manager application and host channel provider |
| `ZECTRIX_ENABLE_UPDATE` | None | Removes streamed firmware writing and commit; core boot protection remains |

`ZECTRIX_ENABLE_BOOK_STORAGE` is derived automatically from Reader, Web book
transfer, USB management or the micro-app runtime. When all four are disabled, SPIFFS and the book storage implementation
leave the build and no library mount is attempted. The partition table and
stored books remain intact. The explicit `books-flash` target is available
when book storage is enabled; normal firmware flash still preserves books.

Disabling a parent disables its dependent options, even if an overlay requests
them. Web book transfer does not depend on the reader or the HTTPS client.
USB management works without Reader, Connectivity or Wi-Fi; see [USB_HOST.md](USB_HOST.md).
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

Set Reader, Runtime and Utilities to `n` as well for the core Launcher, clock, settings, diagnostics,
gallery and sleep covers. Chinese UI remains independently available through
`ZECTRIX_ENABLE_UI_CHINESE`; set it to `n` to remove its glyph subset too.
The committed Minimal profile disables all of these options. Set Connectivity to `y` and Wi-Fi to `n` for a BLE
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

`main/Kconfig.projbuild` sources the component option files even when their
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

Platform registers eight core providers and up to four optional providers:
Connectivity, USB maintenance, the USB host channel and the firmware writer. Typed lookup returns
null for an excluded provider. The Launcher uses the composed application
descriptors, and optional application sources, controllers and renderers are excluded.
Transfer completion returns home when the reader is absent.
With Wi-Fi disabled, RF diagnostics retain SKIP through the summary and do
not count the omitted test as a failure or an executed test.

Offline reading saves the same NVS bookmark format. Its latest revision stays
pending, so a later connected firmware can enqueue it without losing reading
progress. Calendar and sleep-cover snapshots contain only copied display data;
they do not require reader types. The core UI can use a small system Chinese
subset without Reader, or only the compact ASCII face with fallback glyphs for
unsupported Unicode. See [LOCALIZATION.md](LOCALIZATION.md) for language defaults,
persistence and font selection.

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

S1.3 reduces `main/app_main.cc` to the terminal entry. The shell, application
composition and individual applications are separate source files.
`application_modules.cc` binds selected factories only when their required
services are available. A fixed 16-entry `ApplicationCatalog` supplies both
runtime registration and Launcher labels/Open targets. The shell owns factories
and descriptors for the entire runtime lifetime. No menu index is persisted.
Clock remains in Core, including its offline editor; the platform restores its
RTC before Connectivity starts. See [TIME.md](TIME.md).

## Repeatable Full/Minimal regression

S1.4 provides committed profiles in `tools/profiles/`. Full explicitly selects
every optional module. Minimal disables Connectivity, Reader, Runtime, USB maintenance
and firmware writing, including their network and book-storage dependencies.
It retains the Launcher, Clock/editor, Sleep Cover, Settings, gallery,
Diagnostics, device information and mandatory boot protection. SceneManager,
ViewPort and the platform's single-owner lifecycle are unchanged.

```bash
# Runs the complete Host suite and builds/compares both firmware profiles.
bash tools/test-minimal-profile.sh

# Also flashes and checks Minimal, then Full, on the connected Note4.
ZECTRIX_PORT=/dev/cu.usbmodem14301 bash tools/test-minimal-profile.sh --device

# Individual builds or hardware checks use the same profiles.
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile minimal
bash tools/build-firmware.sh --profile full --clean
bash tools/device-smoke-test.sh --profile minimal
```

Named builds use `build-full/` and `build-minimal/`, each with its own
`sdkconfig`. Each invocation regenerates that configuration from the root
`sdkconfig.defaults` plus the committed overlay; a previously saved profile
configuration cannot silently override the selection. Compiled objects and
ccache remain reusable. The ordinary `build/` and root `sdkconfig` are not
changed. For interactive customization, use the separate-build example above
instead of editing these reproducible profile configurations.

Production defaults now select `-Os`. Saved local configurations retain their
own optimization choice. Every firmware build prints application-slot headroom
and writes `firmware-budget.json` from the generated partition table, linked
font symbol and application image. Minimal excludes the packed reader font
and its row-access code. See [FIRMWARE_BUDGET.md](FIRMWARE_BUDGET.md) for current
measurements and the partition/asset tradeoffs.

The Host suite runs once and includes actual Full/Minimal Platform builds,
the Kconfig profile checks, reduced Launcher/runtime navigation, and the
existing offline/connected Reader cases. Optional component unit tests still
run independently of the selected firmware. Repeating this same suite under
two labels would not add coverage.

`build-profile-regression/` contains the Host/build logs, `comparison.log`
and `report.json`. The comparison uses both fresh application binaries and
ESP-IDF's `size --format json` output from the same run. It verifies generated
module settings, the compiled components and source files, and preserved
boot protection and partition tables. Minimal must reduce the application
binary by at least 30% and use less static internal RAM. This is a runnable
regression test against the current Full build, with no stored size baseline
or new release gate.

Static internal RAM is ESP-IDF's `used_dram + used_iram + used_diram`;
the report also gives the data/BSS subset. This avoids double-counting the
ESP32-S3's aliased SRAM and excludes PSRAM and dynamically allocated heap or
task stacks. It is not a runtime free-heap or power measurement.

Hardware smoke checks the selected profile's CLI presence and application
catalog, including a readiness log emitted only after the first Launcher
frame and boot confirmation. Both profiles also check PSRAM, partitions and
startup errors. Raw boot logs are retained as `build-<profile>/device-smoke.log`;
they include the existing internal-heap snapshots. Normal flash preserves
book content. A successful two-profile smoke leaves Full installed. Without a
connected board, `--device` reports SKIP after completing Host/build checks;
a connected device's flash or startup failure remains a test failure.
Physical reading, transfer, sleep/wake and standby-current qualification are
separate from this boot smoke.

The S1.4 run with ESP-IDF 5.5.2 measured:

| Metric | Full | Minimal | Reduction |
| --- | ---: | ---: | ---: |
| Application binary | 2,995,728 bytes | 548,864 bytes | 81.7% |
| Static internal RAM (IRAM + DRAM + DIRAM) | 213,495 bytes | 118,651 bytes | 44.4% |
| Data + BSS subset | 72,168 bytes | 32,156 bytes | 55.4% |

Both build directories started without compiled artifacts and with saved
configurations requesting the opposite module selections. The resulting
images matched their named profiles; the developer's root `sdkconfig` remained
byte-for-byte unchanged. All 32 Host targets passed, including both Platform
profiles, and the module tests cover rejection of a symlinked profile directory
before cleanup. ShellCheck passed for the changed shell scripts.

The connected black-and-white Note4 passed Minimal and Full flash/boot smoke,
reporting nine and twelve registered applications respectively. Full's CLI
log-follow path and Minimal's normal console path both reached the first-frame
readiness marker. At the existing `M3 runtime active` snapshot, free internal
heap was 97,407 bytes in Full and 308,575 bytes in Minimal. These are boot-time
observations, not peak application memory measurements. Both boots tolerated
the invalid retained RTC and kept clock setup available. The run finished with
Full installed; physical RTC retention and standby current remain unmeasured.

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
leaves input files intact. Launcher controllers receive the actual catalog
and are tested with empty, single-item and reduced/full compositions, tile
pagination, overview focus, Tools return, selection restoration and wrap.
Runtime tests open the same descriptor exposed by the menu and verify omitted
destinations cannot be found. See [HOME.md](HOME.md) for the dashboard layout.
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
