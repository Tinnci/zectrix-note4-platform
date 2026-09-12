# Dynamic Lua micro-app pilot

E2.2 adds an optional **Apps** Home destination for independently installed Lua
source files. Calculator and Flashcards are examples in [`apps/`](../apps/).
Installing, exporting or removing one does not rebuild or reflash firmware.

E2.3 adds [`.zapp` packages and the developer CLI](ZAPP_PACKAGES.md), including
icons/metadata, per-app quotas and permissions, and USB/Wi-Fi installation.
Plain `.lua` remains compatible. The package guide describes the current
distribution format; the original guest interface below still applies.

## Runtime choice

This pilot selects **Lua 5.4.9**, one of the alternatives measured in
[E2.1](DYNAMIC_APPLICATION_RESEARCH.md). The WAMR probe could meter callbacks,
but initialization still ran outside that budget and its Host build reported
alignment UB. Lua supports protected parsing, a quota allocator and a count
hook on both initialization and callbacks. Only one engine is shipped.

The existing SDK v1 application registry stays static. A native `apps` adapter
owns private List -> Loading -> Running/Error scenes and one Lua state. Script
filenames are copied into five-entry pages with Previous/Next navigation; they
are not SDK application IDs or retained pointers into a filesystem scan.
Discovery and installation never execute a script.

CrossPoint's incremental reader and bounded transfer work inform the 1 KiB
source-loading slices, object transfers and foreground work scheduling.
Flipper's SceneManager/ViewPort ownership informs deferred private transitions,
the system-owned title/status/navigation areas and deterministic cleanup.
The native streamed reader, sleep covers and Wi-Fi book library retain their
existing owners; a guest cannot access their files or peripherals. The source
references and comparison are recorded in the E2.1 report.

## Install and use

Use a Full firmware build, initialize the content filesystem as described in
[Reader](READER.md), then open **Tools > USB Manager** on Note4. Normal firmware
updates preserve this filesystem. A mount failure never formats it.

```bash
uv run --script tools/usb-manager.py ports
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 app-put apps/Calculator.lua
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 app-put apps/Flashcards.lua
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 app-list
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 app-get Flashcards.lua saved-cards.lua
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 app-remove Flashcards.lua
```

Use the serial port reported by `ports`. Hold OK to return from USB Manager,
then select **Apps** on Home. UP/DOWN selects a filename or page action; OK opens
it. Hold OK exits the guest to its retained list selection, then returns to
Home from the list. Hold DOWN keeps the normal system shutdown behavior.
Errors show a recoverable screen; OK returns to the list. A failed list/mount
can be retried with OK. System UI follows the English/Chinese setting; the two
example scripts contain English text that can be edited independently.

Calculator supports two three-digit unsigned operands, four arithmetic
operators, negative subtraction results and division-by-zero feedback.
UP/DOWN changes the selected digit or operator; OK advances to `=` and runs the
calculation. OK after the result edits again. Flashcards uses UP/DOWN to change
cards and OK to reveal/hide an answer. Edit its `cards` table to carry a small
personal deck, then install the modified file under a new name or explicitly
remove the old copy first. Script state lasts until exit; this pilot has no
persistent guest data API.

Uploads never overwrite an existing name. An interrupted upload is discarded;
an uncertain commit or remove is not retried automatically. Reconnect and
inspect `app-list`/`app-get` before repeating a mutation. Both `.lua` sources and
E2.3 `.zapp` packages carry source compiled at launch. External Lua bytecode,
stores and signing are not supported. Wi-Fi supports both installed formats.

## Guest interface

Scripts define `on_event(key)` and `on_render()`. Initialization runs once.
Return a truthy value from `on_event` only when the displayed state changed.
`on_render` emits a complete frame; the native canvas clears the previous
content. There is no guest timer or idle callback, so a static app needs no
periodic execution or panel refresh.

```lua
local count = 0

function on_event(key)
    if key == note4.UP then count = count + 1
    elseif key == note4.DOWN then count = count - 1
    elseif key == note4.OK then note4.exit()
    else return false end
    return true
end

function on_render()
    note4.text(8, 8, "Counter", 2)
    note4.text(8, 60, count, 2)
end
```

| Host value/function | Behavior |
| --- | --- |
| `note4.api` | Pilot API revision `1`; this is a source interface, not a frozen binary ABI |
| `note4.width`, `note4.height` | `376`, `192`; origin is the guest's upper-left corner |
| `note4.UP`, `note4.DOWN`, `note4.OK` | Copied short-click values `1`, `2`, `3`; long presses stay with the host |
| `note4.text(x, y, text, scale, style)` | Black UTF-8 text or number; optional integer scale `1` or `2`, optional style flags defaulting to `0`, at most 128 bytes per call |
| `note4.REGULAR`, `BOLD`, `ITALIC`, `DIM`, `UNDERLINE`, `KEYCAP` | Values `0`, `1`, `2`, `4`, `8`, `16`; combine with Lua's `|` |
| `note4.rect(x, y, width, height)` | Black rectangle outline |
| `note4.fill(x, y, width, height)` | Black filled rectangle |
| `note4.exit()` | Request return after initialization/event callback completes |

Drawing is permitted only in `on_render`. Coordinates must be integers inside
the viewport; rectangle dimensions are nonnegative and cannot cross it. Text
is clipped to the viewport, without wrapping. Unsupported glyphs use the
firmware font's fallback; Full includes the broad reader CJK font. At most 64
commands and 2,048 copied text bytes (including terminators) form one frame.
The host draws at `(12,66,376,192)` in the shared 15,000-byte canvas. No guest
pointer, raw framebuffer, display waveform or hardware object is exposed.

R1.3 adds [algorithmic typography](TYPOGRAPHY.md) to these copied frames.
Calculator emphasizes its result and boxes its edit hint; Flashcards emphasizes
questions, italicizes answers and dims the card index. Four-argument scripts
keep Regular text. Styling adds no font face or rendering allocation.
Unsupported flag bits and negative styles use the existing drawing-error path.
The host intersects its content region with the caller's clip before painting.

The Lua language supports tables, functions, numeric loops, operators, `#` and
string concatenation. No standard libraries are opened: `io`, `os`, `package`,
`debug`, `coroutine`, `load`, `require`, `setmetatable`, `pcall`, `print`, `pairs`
and similar library functions are absent. The host table is local to each VM;
changing its fields grants no additional access. Use numeric loops over arrays
and the drawing functions shown above.

## Bounds and lifecycle

| Resource | Bound / owner |
| --- | --- |
| Installed source | 1–32,768 bytes; UTF-8 basename of at most 47 bytes ending in `.lua` (case insensitive), no slash or control characters |
| Installed package | `.zapp`, at most 33,056 bytes including metadata and 16/32-pixel icon; same filename bound; source size remains 1–32,768 bytes |
| Loading | One file and a source buffer; read at most 1 KiB per idle callback, yield one RTOS tick between slices |
| Lua allocation | 128 KiB including the runtime's allocation headers; PSRAM on ESP32-S3, accounted allocation on Host; underlying heap-manager metadata is additional |
| Execution | At most 10,000 Lua instructions per initialization/event/render invocation; packages may request 100–10,000; count-hook yield terminates the instance |
| Native parser/C calls | `LUAI_MAXCCALLS=16`; parsing also has the source/heap bounds |
| Foreground work | Serial SDK callbacks; no extra task, radio, guest filesystem or background timer |
| Drawing | One complete copied command buffer; only physical presentation is retried after a display failure |

Source allocation happens on entry to Loading, after the previous foreground
has exited. The source and file close after parsing; the Lua allocator remains
only while the guest runs. Back during loading, source errors, guest errors,
quota exhaustion, explicit exit and SDK shutdown all close handles and free
memory. Stop and destruction are idempotent. Scene transitions apply after
the callback returns. A failed frame is never interpreted as a successful guest
render, and retrying physical presentation does not execute guest code again.
Faults log a bounded sanitized diagnostic and peak guest heap through the
existing firmware log.

Xtensa `-Os -fstack-usage` inspection reported 144 bytes for the parser's `body`,
112 for `statement`, and 208 for `luaY_parser`. The recursion limit is 16 to
leave room for the owner and error/allocator paths in the existing 8 KiB main
task stack. This is compiler stack analysis, not a physical stack high-water
measurement. Lua parsing, garbage collection, bounded drawing, filesystem I/O
and display completion remain synchronous native work; the instruction limit
is not a wall-clock guarantee. Software VM isolation does not provide an MMU
boundary against bugs in the native interpreter.

## Storage and USB ownership

`AppStorage` is a façade over the existing `BookStorage` mount and management
lease, with physical names `.app-<name>.lua` or `.app-<name>.zapp`. Public book name validation still
accepts only TXT/EPUB. Listings, reads, uploads and removals resolve their own
namespace; app operations cannot target books. Directory scans retain only a
sorted page. A previous-page scan selects the preceding bounded suffix and
returns it in ascending order.

USB uses the existing request slot, chunk offsets, staging file, exact length,
flush/fsync, no-overwrite rename and space reserve. An open reader or app source
excludes management; management excludes app loading. No second mount, mutex,
raw partition writer or format operation is added. See [USB_HOST.md](USB_HOST.md)
for operations 12–15. Old book operations and the 16-byte Info reply keep their
wire layouts. Firmware with the runtime disabled returns `Unavailable` for
app operations; earlier firmware without these operations rejects them.

## Build and verification

`CONFIG_ZECTRIX_ENABLE_RUNTIME` defaults to enabled in Full and is disabled in
Minimal. It implies content storage, but does not require Reader, Chinese UI,
Connectivity or USB. Without USB, preinstalled sources can still run: place
`.app-<name>.lua` files in a separately prepared content-image directory. App
transfer is disabled in the UI for that build. Preparing/flashing a whole
content image replaces its contents; normal `app-put` is the incremental path.

The build fetches the Lua `v5.4.9` release. To reuse a local checkout, pass
`-DZECTRIX_LUA_SOURCE_DIR=/absolute/path/to/lua` to CMake/`idf.py`, or export that
variable for the Host script. Lua's license is included in
[third-party notices](../THIRD_PARTY_NOTICES.md).

```bash
bash tools/test-runtime.sh
ZECTRIX_RUNTIME_SANITIZE=1 bash tools/test-runtime.sh
ZECTRIX_USB_SANITIZE=1 UBSAN_OPTIONS=halt_on_error=1 bash tools/test-usb-manager.sh
bash tools/test-host.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

The runtime tests exercise both real pilots, malformed/binary source, nested
parser limits, initialization/event/render loops, allocator exhaustion at
multiple limits, text/geometry/command bounds and 1,024 VM fault/restart lifetimes.
Each repeated pilot run alternates instruction-limit or heap-exhaustion faults
and verifies that live guest allocation returns to zero before reopening. Controller
tests use real file storage for namespace isolation, forward/backward paging,
loading cancellation, truncated reads, guest/system exits, retry without guest
reexecution, clipped drawing and 100 foreground lifetimes. They write
`calculator.pbm` and `flashcards.pbm` into the Host build directory for visual
inspection. USB tests use the production protocol over a PTY and the shipped
CLI to install/export/remove both examples and preserve interrupted-transfer,
no-overwrite and runtime-disabled behavior. These are Host simulations, not
measurements of physical USB throughput, button latency or standby power.

Verification on 2026-09-11: all 36 Host targets passed, including English/Chinese
rendering and existing reader, transfer, lifecycle and boot-protection tests.
Runtime and USB checks passed ASan/UBSan; the USB suite includes eight Python
client/PTY scenarios. Both pilot previews were inspected. Full/Minimal builds
and the existing profile comparison passed with these results:

| Metric | Full | Minimal |
| --- | ---: | ---: |
| Application image | 3,112,640 B | 563,872 B |
| Static internal RAM | 213,567 B | 118,731 B |
| Data + BSS | 72,240 B | 32,236 B |

The Full image fits the existing 3 MiB slot with 33,088 bytes remaining, an
85,408-byte increase over the E2.1 production image. Minimal excludes Lua and
the Apps controller/renderers, and is 81.9% smaller than Full. The partition
layout and mandatory boot protection are unchanged. This iteration did not
flash hardware or claim physical guest execution qualification.
