# Firmware capacity and font storage

The S1.2 firmware-capacity iteration uses size optimization and shared bitmap
tiles. It preserves the installed partition table, factory recovery, A/B OTA,
NVS and the 4 MiB book store. Existing devices can receive this application
through the current OTA service. No content migration or separate asset flash
is needed.

## Measurements and implementation

D1.4 produced a 3,139,120-byte Full image. The smallest application slot has
3,145,728 bytes. Only 6,608 bytes remained. The Unifont reader data accounted
for 1,325,973 bytes of that image.

The compiler had used ESP-IDF's default `-Og` setting. A separate `-Os` build
with the original font measured 2,983,008 bytes. `sdkconfig.defaults` now selects
`CONFIG_COMPILER_OPTIMIZATION_SIZE`. Assertions, debug information, certificate
validation and boot protection stay enabled. The compiler exposed a Launcher
page-label truncation warning. Its buffer now fits both complete unsigned
values. Existing function/data sections, unused-section removal, disabled C++
exceptions and disabled RTTI already reduce unused code.

The reader font now occupies 824,959 bytes, a reduction of 501,014 bytes (37.8%).
The generator splits each glyph into four 8x8 tiles and stores each distinct
tile once. There are 62,311 tiles. Four 16-bit indices select a glyph's tiles.
A separate width bit supplies its 8px or 16px advance.

`GlyphWidth()` reads that bit directly. `GlyphBitmap()` returns four immutable
Flash pointers and the width. `Row()` combines two tile bytes. Later lookups do
not invalidate a view. The implementation has no mutable cache, decompression
workspace, heap allocation, file handle or background task. A view occupies
20 bytes on ESP32-S3. The renderer keeps the same 16px and scaled 24px pixels.
The public SDK and stored reading positions do not change.

Raw DEFLATE was also measured. Independent 32-glyph blocks would use about
745 KB, including their index. Whole-font compression used 573 KB. Those
formats require decode work and a workspace before random glyph access.
The tile representation provides direct row access for pagination, UI labels
and rendering with about 825 KB of Flash. See the [font notice and packing
layout](../components/zectrix_reader/font/README.md).

## Partition and asset alternatives

The current allocation ends at `0xd12000`. It leaves 3,072,000 bytes unallocated
on the configured 16 MiB Flash. Addresses come from [partitions.csv](../partitions.csv).

CrossPoint's current [16 MiB layout][crosspoint-layout] uses two 6.25 MiB OTA
slots, a 3.375 MiB SPIFFS store and a core-dump partition. It has no separate
factory slot. Note4 retains its factory fallback and 4 MiB user store, so those
slot sizes do not apply directly to this product.

| Option | Capacity and consequence | Decision |
| --- | --- | --- |
| Current factory + two 3 MiB slots, packed font and `-Os` | Recovers application space without moving any persistent data | Ship this combination |
| Three 4 MiB application slots + 4 MiB books | Exceeds 16 MiB by 72 KiB after boot/partition space and OTA metadata | Does not fit |
| Three `0x3f0000` slots + 4 MiB books | Adds 960 KiB per slot and leaves 120 KiB unallocated. Moves both OTA slots, OTA metadata and books | Feasible later migration |
| 3 MiB factory + two 4 MiB OTA slots | Keeps a smaller factory limit for a shared Full image. Requires a separate recovery-image workflow to use larger Full images | No benefit for the current single-image build |
| Shared font data partition | Removes the font from each application image. Adds installation, mapping and rollback compatibility requirements | Keep fonts with each application for now |

The equal-slot expansion would use these addresses:

| Partition | Offset | Size |
| --- | ---: | ---: |
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xf000` | `0x1000` |
| `factory` | `0x10000` | `0x3f0000` |
| `ota_0` | `0x400000` | `0x3f0000` |
| `ota_1` | `0x7f0000` | `0x3f0000` |
| `otadata` | `0xbe0000` | `0x2000` |
| `books` | `0xbe2000` | `0x400000` |

This table is an evaluated alternative, not the installed layout. Writing its
new OTA metadata into an old device would overwrite existing book data.
Application OTA cannot relocate that data or replace the bootloader's table.
A future migration needs a recoverable copy of user data, complete USB image
installation and content restoration. The current reduction avoids that
transition. Normal firmware flash continues to preserve books and NVS.

A shared mutable font also introduces a concrete rollback problem. Power loss
between writing the font and selecting an application can leave the old image
with an incompatible font. Separate versioned asset banks or a compatible
asset lifetime can solve that problem. They cost Flash and provisioning work.
Embedding the packed font keeps each image and its font together under the
existing image validation and A/B rollback mechanism.

ESP-IDF 5.5.2 has no supported LTO Kconfig selection. This iteration uses its
supported size optimization. Adding `-flto` globally would need qualification
of the mixed prebuilt radio libraries and IRAM placement. No size benefit from
LTO is claimed. NimBLE already has one connection, bond and CCCD slot. TLS
keeps its current certificate bundle, chain, hostname and date checks.

## Build and observation

Run the named profiles to use the committed production settings:

```bash
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
uv run --no-project tools/compare-firmware-profiles.py build-full build-minimal \
  --output build-full/profile-comparison.json
```

The ordinary local build preserves saved `sdkconfig` choices. A saved debug
configuration can still use `-Og`. The report identifies the selected mode.

Every build writes `firmware-budget.json` in its build directory. The reporter
reads the generated native partition table and flasher settings, measures the
actual application binary and reads the linked font symbol. It lists each
application slot, remaining capacity and unallocated Flash regions. It does
not introduce a size threshold. ESP-IDF still checks that the image fits.

`device-smoke-test.sh` still flashes the generated ESP-IDF image set. Its boot
check compares every partition name, address and size with the build report.
The report and smoke test do not contain a second set of application or book
offsets. Host tests exercise the current,
expanded and asymmetric layouts.

## Reader and scene ownership

CrossPoint's [streamed reader][reader], [sleep composition][sleep] and
[Web file/font transfer][web] favor bounded work and explicit resource
lifetimes. Its external-font workflow is useful when storage and provisioning
support it. Note4's book store also holds micro-apps and user files. Keeping the
boot/UI font inside the application avoids a new dependency on that store.

Flipper Zero's [SceneManager][scenes] and [ViewPort][views] retain scene and
drawing ownership. Note4 keeps the existing foreground owner and shared
15,000-byte canvas. The font views are immutable, so reading, sleep-cover
composition and viewport callbacks need no new cache owner or cleanup path.

## Verification results

The ESP32-S3 Full and Minimal builds passed with the same partition table.

| Metric | Before (D1.4) | After (S1.2 capacity) |
| --- | ---: | ---: |
| Full application | 3,139,120 bytes | 2,482,208 bytes |
| Full slot free space | 6,608 bytes | 663,520 bytes (21.1%) |
| Minimal application | 565,392 bytes | 514,528 bytes |
| Full static internal RAM | 213,895 bytes | 201,815 bytes |
| Minimal static internal RAM | 118,731 bytes | 108,315 bytes |

Full saves 656,912 application bytes. Minimal is 79.3% smaller than Full and
contains no reader font symbol. Static RAM uses the existing ESP-IDF profile
calculation described in [MODULAR_BUILD.md](MODULAR_BUILD.md).

All 39 Host targets passed. Font and Reader ASan/UBSan checks passed. Every
width and pixel of all 40,181 production glyphs matched the original raw font.
Regenerating from the upstream Unifont 15.1.05 BDF produced the same packed
asset and raw pixels. The English/Chinese UI tests passed at both reader sizes.
A 640-glyph random row-read workload took 25.1 microseconds per page on Host
with `-O2`. This measures font access, not device rendering or panel latency.

The actual 2,482,208-byte Full image passed the Host OTA driver with 997-byte
chunks, native-header checks and readback verification. Profile comparison and
ShellCheck passed. No device was flashed for this iteration. Hardware timing,
power and recovery fault injection remain separate qualification work.

[reader]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp
[sleep]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp
[web]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
[scenes]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c
[views]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c
[crosspoint-layout]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/partitions.csv
