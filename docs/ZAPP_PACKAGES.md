# Note4 application packages

E2.3 adds `.zapp` distribution to the [Lua micro-app runtime](MICRO_APPS.md).
A package contains its display name, app version, author, monochrome icon,
permissions, instruction quota and source in one file. It can be installed,
exported and removed through USB Manager or the existing Send Books web page.
Full enables Apps; firmware without the runtime rejects app transfers.

The first format carries **UTF-8 Lua source**. Lua 5.4.9 compiles it in the
bounded VM when the user opens the app. Portable source avoids host/target
bytecode representation differences and preserves the existing text-only
loader. The CLI validates the container and text; it does not compile Lua,
check Lua syntax or execute the app. Syntax and guest behavior are checked at
launch. Serialized Lua bytecode and native executables remain unsupported.

## Build and distribute

The CLI uses Python 3.11+ through uv and has no third-party dependencies.
Run from the repository, or add its `tools` directory to `PATH` to use `zapp`.

```bash
tools/zapp build apps/Calculator.app.json -o build-zapp/Calculator.zapp
tools/zapp build apps/Flashcards.app.json -o build-zapp/Flashcards.zapp
tools/zapp inspect build-zapp/Calculator.zapp

tools/zapp pack apps/Calculator.lua --icon apps/Calculator.pbm \
  --name Calculator --version 1.0.0 --author Note4 \
  --permission display --permission input --quota 10000 \
  -o build-zapp/Calculator.zapp
```

`uv run --script tools/zapp.py ...` is the direct equivalent. `build` accepts
a manifest path or a directory containing `app.json`; paths inside the manifest
are relative to that manifest. Without `-o`, output is `dist/<entry-stem>.zapp`
beside the entry source. A successful local build atomically replaces its output;
a failed build preserves the old file. Device installation never overwrites.
`inspect` validates the complete file and prints JSON metadata without extracting
or running it. App filenames must fit 47 UTF-8 bytes, including `.zapp`.

```json
{
  "format_version": 1,
  "guest_api": 1,
  "name": "Flashcards",
  "version": "1.0.0",
  "author": "Note4",
  "entry": "Flashcards.lua",
  "icon": "Flashcards.pbm",
  "permissions": ["display", "input"],
  "instruction_quota": 5000
}
```

`name`, `version`, `author`, `entry`, `icon` and `permissions` are required.
`format_version` and `guest_api` default to `1`, and `instruction_quota` defaults
to `10000`. Unknown fields, unsupported permissions and duplicate permissions
are rejected to catch mistakes. Icons use 16×16 or 32×32 PBM, either ASCII P1
or packed P4; `1` means black. The two example manifests and original icons
are in [`apps/`](../apps/). Calculator packages to 2,019 bytes and Flashcards
to 1,392 bytes, including their source.

For USB, open **Tools > USB Manager** on Note4, then run:

```bash
uv run --script tools/usb-manager.py ports
uv run --script tools/usb-manager.py --port PORT app-put build-zapp/Calculator.zapp
uv run --script tools/usb-manager.py --port PORT app-list
uv run --script tools/usb-manager.py --port PORT app-get Calculator.zapp exported.zapp
uv run --script tools/usb-manager.py --port PORT app-remove Calculator.zapp
```

For Wi-Fi, open **Send Books**, connect to its hotspot or saved network, open
the displayed address and enter the screen code. Drop `.zapp`, `.lua`, TXT
and EPUB files together and choose **Upload & finish**. The page routes each
file to its namespace, lists installed apps and supports download/deletion.
An app upload failure stops the batch and retains earlier completed files.
After leaving transfer mode, return to **Home > Apps** and press OK to launch.
The list shows package names, icons and versions; the selected app's author
appears above it. Raw `.lua` files keep their filename-based display.

## Version 1 binary layout

All integers are unsigned little endian. The header is exactly **160 bytes**,
followed by the icon and then the source, with no alignment gaps, compression,
archive paths, trailing data or executable section table.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII `ZAPP` |
| 4 | 2 | Package format version, `1` |
| 6 | 2 | Guest API revision, `1` (`note4.api`) |
| 8 | 1 | Payload kind, `1` for Lua source |
| 9 | 1 | Icon width and height, `16` or `32` |
| 10 | 2 | Permissions: bit 0 `display`, bit 1 `input`; all other bits zero |
| 12 | 4 | Instruction quota, `100`–`10000` |
| 16 | 4 | Source byte length, `1`–`32768` |
| 20 | 4 | Reserved, zero |
| 24 | 64 | UTF-8 display name, at most 63 bytes |
| 88 | 24 | App version, at most 23 visible ASCII bytes |
| 112 | 48 | UTF-8 author, at most 47 bytes |
| 160 | 32 or 128 | Icon: top-to-bottom rows, leftmost pixel in the most significant bit |
| 192 or 288 | Source length | UTF-8 Lua source |

The three strings are nonempty, NUL-terminated and zero-padded to their field
widths. Name and author reject ASCII control characters. The app version is a
human-readable label (for example `1.0.0`), independent of package/API versions.
Source permits tab, CR and LF but rejects other ASCII control characters,
invalid UTF-8, NUL and Lua's binary chunk escape marker. Exact lengths reject
truncation and appended data. Maximum package size is **33,056 bytes**.

Package/API versions are checked before execution. An unsupported payload kind,
permission, reserved field, icon size or quota is rejected during installation
and checked again when opening. App version/name/author are self-declared
metadata, not publisher authentication. Filename remains the storage identity;
renaming a package installs a separate copy. There is no automatic upgrade,
downgrade, signing, store or dependency-resolution service in this format.

## Permissions, bounds and ownership

`display` permits the existing `note4.text`, `rect` and `fill` operations during
`on_render`. Calling one without the declaration terminates the guest with a
permission error. Without `input`, short clicks never invoke `on_event`.
`on_render` still runs and can produce an empty frame without `display`.
System Back/shutdown always remain available. There are no file, radio, timer,
OTA, raw framebuffer or native hardware capabilities to declare.

The quota applies independently to initialization, each event and each render.
It can lower the existing 10,000-instruction ceiling and cannot raise it.
The 128 KiB Lua heap, recursion limit, copied drawing bounds and no-standard-
libraries environment remain in force. Raw `.lua` files retain both existing
permissions and the 10,000-instruction budget. USB inspection reports the actual
selected quota. Instruction accounting is not a wall-clock execution limit.

`AppStorage` keeps the whole package at `.app-<filename>.zapp` in the existing
content partition. It shares the book mutex, management lease, 1 KiB transfer
buffers and `.upload.part` staging file. A streaming validator retains a
160-byte header and UTF-8 state, ignores icon bytes after length validation and
validates source across chunk boundaries. Invalid input poisons the upload;
publication still requires the exact length, flush/fsync, close and no existing
destination. Cancellation/error cleanup and interrupted-boot staging recovery
use the existing paths. No code executes during discovery or installation.

Discovery reads at most 288 bytes per package into a five-entry metadata page.
Malformed on-disk entries retain their filename and open to a recoverable error.
Launch reopens and validates the header, allocates only the declared source in
PSRAM and reads it in 1 KiB idle slices. It validates source text again before
calling the text-only Lua loader. The file/source buffer close after loading;
Back during loading, errors and shutdown release them. No unpacked side files,
new task, partition, application registry or per-app framebuffer are needed.
Physical display retries keep using the copied frame without guest reexecution.

The 64-bit Host build measures the streaming validator at 192 bytes, each
metadata entry at 276 bytes, the complete Apps controller at 5,408 bytes and
USB BookSession at 2,944 bytes (+200). These foreground objects are distinct
from the source/VM allocations and are not static ESP32 RAM measurements.

## Design references and verification

Reviewed Flipper's [FAP documentation][fap] and existing SceneManager/ViewPort
source on 2026-09-12. Its single-file metadata/assets and separate API identity
inform this format. Note4 retains its bounded Lua environment; it does not
import FAP's native ELF relocations or symbol-table generation. CrossPoint's
[streamed web uploads][crosspoint] and incremental reader inform fixed-chunk
transfers and foreground loading slices. Existing static sleep covers, streamed
pagination and radio ownership continue through their native owners. The wider
comparison is in [the runtime research](DYNAMIC_APPLICATION_RESEARCH.md).

```bash
bash tools/test-zapp.sh
ZECTRIX_ZAPP_SANITIZE=1 bash tools/test-zapp.sh
ZECTRIX_RUNTIME_SANITIZE=1 bash tools/test-runtime.sh
bash tools/test-usb-manager.sh
bash tools/test-book-transfer.sh
bash tools/test-host.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

Tests use CLI-built packages with the independent C++ parser, real file storage,
Lua engine, USB PTY and HTTP server. Coverage includes 46 valid/malformed
containers, 16/32 icons, full-size sources, UTF-8 split across chunks, unsupported
versions/permissions, poisoned uploads, no-overwrite publication, disconnect and
cancellation cleanup, exported byte equality, app/book separation, disabled
runtime, discovery without execution, quota/permission enforcement and copied
render retries. Host simulation does not qualify physical flash power loss,
USB/RF throughput, display latency or sustained hardware execution.

Verification on 2026-09-12: all 41 Host targets, package/runtime ASan/UBSan and
ShellCheck passed. Chrome desktop/mobile checks exercised actual drag-and-drop
of both example packages with a book, completed session shutdown, byte-exact
browser download and confirmed app deletion preserving other files. Screenshots
were inspected at 1280-pixel and 390-pixel widths. Full/Minimal builds and the
existing profile comparison passed:

| Metric | Full | Minimal |
| --- | ---: | ---: |
| Application image | 2,498,800 B (+3,488) | 521,680 B (+32) |
| Static internal RAM | 201,815 B (unchanged) | 108,323 B (unchanged) |
| Data + BSS | 72,112 B (unchanged) | 31,824 B (unchanged) |

This iteration did not flash hardware. Existing physical USB/RF, power-loss and
long-running device qualifications remain separate from these Host checks.

[fap]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/AppsOnSDCard.md
[crosspoint]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
