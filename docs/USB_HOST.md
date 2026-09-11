# USB books and device management

E1.8 implements a host bridge on Note4's existing USB Serial/JTAG connection.
The maintenance prompt and a bounded binary session share one transport owner.
**Tools > USB Manager** owns the book library and device settings while a
computer lists, imports or exports books. No new task, partition, filesystem,
USB descriptor or public application SDK API is required.

E2.2 adds independently installed [Lua micro-apps](MICRO_APPS.md) through the
same owner. `app-list`, `app-put`, `app-get` and `app-remove` operate on the app
namespace; the existing book commands and settings keep their behavior.

## Transport choice

Source review: 2026-09-10. The comparison distinguishes host class support from
the software needed to perform Note4 operations.

| Approach | Desktop / Android access | Storage and device cost | E1.8 decision |
| --- | --- | --- | --- |
| MSC, raw disk | macOS, Windows and Linux normally provide storage drivers; Android needs USB host support and a recognized filesystem | The host owns block allocation and metadata. Firmware must close every handle and relinquish the volume until eject/disconnect. Note4's SPIFFS image is not a host-readable FAT disk. ESP32-S3 OTG/TinyUSB also requires handing over the shared internal PHY | Keep as a possible future FAT/SD-volume mode. Do not expose the live SPIFFS partition as a writable disk |
| MTP, object access | Windows has Explorer integration; macOS normally needs an additional client; Linux support depends on libmtp/GVfs/KIO. Android USB-host MTP access depends on its app and vendor implementation | Firmware retains filesystem ownership, but must implement object handles, discovery, sessions, transactions, events and compatible host behavior, plus the configurable OTG USB stack | The object-ownership model fits; a full MTP implementation is disproportionate to the current book library |
| Serial/JTAG with framed objects | Serial ports on macOS (`/dev/cu.*`), Windows (`COM*`) and Linux (`/dev/ttyACM*`); the supplied tool needs Python and pyserial, provisioned by uv. Android needs an OTG-capable app with USB permission and a serial adapter | Reuses the installed hardware function and CLI task. Fixed packets, stop-and-wait backpressure, and the existing management lease keep memory and lifetime explicit | Implemented. Desktop browser WebSerial and an Android USB client can later use the same typed operations |

ESP-IDF documents Serial/JTAG as a **fixed-function** controller: its USB
descriptors cannot be extended with MSC or MTP. The S3 also has a configurable
USB-OTG controller; using it is a separate PHY/controller/lifecycle decision.
The internal native USB pins remain GPIO19/20. Browser WebSerial availability
and permission behavior vary; there is no universal mobile-browser or Android
support claim in this delivery. Only macOS PTY integration and the recorded
device checks below have been run locally.

CrossPoint's [UsbDriveActivity][cp-usb] displays instructions before giving the
raw SD card to the host, limits Back to waiting/startup-error states, handles
eject/disconnect and restarts after storage handoff. Its S3-specific
[Serial/JTAG handoff][cp-handoff] deinitializes OTG, changes PHY selection,
forces re-enumeration and waits for bus reset. This is appropriate for a
dedicated disk mode, with costs that a live SPIFFS object service avoids.
CrossPoint's [web server][cp-web] also demonstrates bounded upload chunks and
short-write handling, which align with Note4's existing BookStorage API.

Flipper's [RPC service][flipper-rpc] uses an explicit `start_rpc_session`
command, framed messages and disconnect cleanup; its [VCP transport][flipper-vcp]
uses bounded pipes and connection events. Note4 adopts the explicit session and
single-owner pattern with fixed storage. The existing deferred application
runtime and [SceneManager/ViewPort rules](M3_APPLICATION_CONTRACT.md) already
provide navigation and rendering. A single dashboard needs no extra scene
stack, render task or controller-switching state machine. Upstream code was
studied, not imported.

## Use

Install the initial book filesystem as described in [READER.md](READER.md),
then open **Tools > USB Manager**. A failed mount never formats the library.
Use a data cable and close any other serial monitor using this port.

```bash
uv run --script tools/usb-manager.py ports
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 info
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 list
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 put novel.epub
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 put notes.txt --name notes-2.txt
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 get novel.epub exported.epub
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 get-setting language
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 set-setting language zh-CN
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 set-setting auto_showcase off
uv run --script tools/usb-manager.py --port /dev/cu.usbmodem14301 set-setting sleep_cover landscape
```

Substitute the port reported by `ports`, for example `COM5` or `/dev/ttyACM0`.
Linux may require membership in the distribution's serial-device group.
Language accepts `en`/`zh-CN`, auto-showcase `off`/`on`, and sleep cover
`dashboard`/`landscape`/`blank`. A language excluded by Kconfig is rejected.

Each invocation waits for the maintenance prompt, negotiates its session,
executes the command, and sends Close to restore the prompt. Ctrl+C in the
computer tool follows the same cleanup path. Within binary mode, control
characters are ordinary payload bytes. A crashed client that loses its session
ID may require a cable reconnect before the next terminal session; closing a
TTY is not guaranteed to generate a Serial/JTAG physical disconnect.

Uploads never replace a same-name book. Choose another name or use the existing
Web library's explicit delete action. Downloads stage beside the destination
and publish with an exclusive hard link, preserving a file created during the
transfer. A local filesystem without hard-link support reports failure and
cleans the staging file. The tool does not retry an uncertain upload commit or
setting change: inspect the book/setting before repeating it.

## Ownership, buttons and cleanup

```text
USB worker: CliSession -> Protocol -> Channel (one copied request/reply)
                                            |
foreground: USB Manager -> BookSession ------+
                         -> UsbSettings -> existing shell preferences
                         -> BookStorage management lease
                         -> existing UI/display scheduler
```

Platform owns Channel and Protocol for the maintenance lifetime. USB Manager
entry obtains `StorageService::BeginBookManagement`; Reader handles and the
Wi-Fi management owner exclude that lease. A failed acquisition leaves a
retryable screen. Only entry with an acquired lease enables host admission.
There is no per-file confirmation dialog.

The USB worker owns parsing and output, and never calls Storage, settings or
ApplicationRuntime. One idle callback takes a copied request and performs at
most one file chunk or typed operation. A slot that is executing cannot be
reused after cancellation until its owner returns. Session and request IDs
reject stale traffic; no request carries a pointer into a foreground app.

| Input / event | Behavior |
| --- | --- |
| Short OK | Cancel the current host session and close/abort its open transfer; on failed library entry, retry acquisition |
| Hold OK | Deferred Back to the retained Tools selection; exit releases the library |
| Hold DOWN | Existing shutdown command; exit cleans USB work before the final sleep cover and power transition |
| UP/DOWN clicks, hold UP | No dashboard action |
| Clean host Close or observed physical disconnect | Retire host admission; foreground closes the transfer at its next safe point and returns to waiting |
| 30 seconds without a valid request | Retire the session and abort unfinished work at the next foreground safe point |
| Frame error or TX failure | Keep binary input isolated; do not interpret trailing file bytes as terminal commands |

The foreground continues using the shared button buffer and display scheduler.
While a host session exists, both the shell and USB worker yield one RTOS tick
between polls. An idle dashboard retains the normal 250 ms shell wait. Progress
uses 10-percent buckets; ordinary progress, filenames and short-file phase
changes coalesce to at most one refresh per second. Entry, errors, local cancel,
settings changes and failed-render recovery request Quality promptly. They do
not wait for a transfer to finish. Physical display BUSY and synchronous SPIFFS
operations still bound input-to-action latency; this is not asynchronous I/O.

BookSession retains one upload or download handle. Upload data goes through
the existing `.upload.part` file, exact offsets and declared total length.
Commit requires the full byte count, flush/fsync, close and a final destination
existence check before rename. Abort, exit and disconnect remove only that
reserved staging file. The conservative SPIFFS space limit and no-format mount
policy remain in place. The active app retains its management lease between
computer commands; Back releases it before Reader or Send Books enters.

Settings use the existing keys and validation. Language and sleep-cover changes
update the live shell preferences even when persistence fails; `NotSaved`
reports this explicitly, and repeating the selection retries the write.
Auto-showcase reports an I/O error if its NVS update fails. Language changes
redraw content/status together; shutdown reads the updated cover preference.
These operations do not grant pairing, OTA, raw partition or arbitrary memory
access, and do not change the future D1 confirmed-mutation policy.

## Version 1 wire format

The text command is `host start 1`. The peer waits for
`N4USB 1 <session-id> 1024\r\n` before sending data; pipelining bytes behind the
start command is unsupported. Unsupported versions or an unavailable manager
return an ordinary CLI error without entering binary mode.

Each decoded frame contains this header, then exactly `length` payload bytes.
All multi-byte integers use little endian. Encode the whole frame with COBS,
then append one zero delimiter; empty delimiters are ignored.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII `N4U1` |
| 4 | 1 | Operation; replies set bit `0x80` |
| 5 | 1 | Status; requests use zero |
| 6 | 2 | Payload length, 0–1024 |
| 8 | 4 | Positive, strictly increasing request ID |
| 12 | 4 | Session ID from the greeting |

The largest encoded frame is 1,046 bytes including its delimiter. There is one
request in flight; wait for its complete matching response before sending the
next request. USB supplies link error detection/retry. This protocol uses
lengths, IDs and offsets and adds no checksum or content hash. It does not
provide resume, content authentication or automatic commit replay.

Names and cursors are UTF-8 bytes without a terminator, at most 63 bytes, and
must pass `BookStorage::ValidName` (`.txt`/`.epub`, no path/control characters).
Transferred contents retain the existing reader's format limitations.

| Op | Request payload | Successful response |
| --- | --- | --- |
| 1 Info | Empty | Four u32 values: total, used, available, upload chunk size (1020) |
| 2 List | Empty or last filename from previous page | u8 count (0–8), u8 more, then each entry: u8 name length, u32 file size, name bytes |
| 3 ReadOpen | Filename | u32 file size; opens one download |
| 4 Read | u32 next offset | Next `min(1024, remaining)` bytes; EOF closes the file |
| 5 UploadBegin | u32 total size, filename | Empty; zero-length books are allowed |
| 6 UploadChunk | u32 next offset, 1–1020 data bytes | u32 next offset |
| 7 UploadCommit | Empty | Empty after successful commit; incomplete/failed commits abort staging |
| 8 Abort | Empty | Empty; closes transfer but retains the host session |
| 9 GetSetting | u8 key | u32 current value |
| 10 SetSetting | u8 key, u32 value | Empty |
| 11 Close | Empty | Empty, followed by the text maintenance prompt |
| 12 AppList | Empty or last app filename | Same paged entry layout as List, for apps only |
| 13 AppReadOpen | App filename | u32 source size; following Read operations export this app |
| 14 AppUploadBegin | u32 source size, app filename | Empty; following UploadChunk/UploadCommit install this app |
| 15 AppRemove | App filename | Empty after removal; rejected while a transfer is open |

App names pass `AppStorage::ValidName`: at most 47 UTF-8 bytes with `.lua`
suffix, without path/control characters. Sources contain 1–32,768 bytes. The
foreground never executes code during these operations. They share the one
transfer handle and storage lease with books and return `Unavailable` when
the runtime module is disabled. USB Manager reports imported files and app
removal. App remove has the same explicit uncertain-outcome treatment as a
commit: inspect the list before repeating it after cancellation or timeout.

Setting keys are `0` language, `1` auto-showcase and `2` sleep cover. Values
match [M3 platform settings](M3_APPLICATION_CONTRACT.md#m3-platform-settings).
Status codes are `0 Ok`, `1 Invalid`, `2 Busy`, `3 Unavailable`, `4 Exists`,
`5 NotFound`, `6 NoSpace`, `7 IoError`, `8 Cancelled`, `9 Timeout`, `10 NotSaved`.
Unsuccessful responses carry no payload.

A session-level error uses operation `0x80`, request ID zero and a status.
The parser still owns binary input and accepts a correctly framed Close with
the session ID and a newer request ID; send a zero delimiter first to discard
an incomplete frame. A TX failure requires physical reconnect if the Close
reply cannot be delivered. The text session discards buffered tail bytes when
Close completes. Cancellation during an executing commit or setting update can
have an unknown outcome; a timeout alone never proves rollback.

## Resource bounds and verification

`CONFIG_ZECTRIX_ENABLE_USB_HOST` depends on USB CLI and defaults on in Full.
It enables book storage independently of Reader/Wi-Fi. Minimal removes its
provider, protocol, application and renderer. The protocol/mailbox use fixed
arrays; neither framing nor drawing allocates per packet. File open and the
existing stdio/SPIFFS implementation can allocate their own handles/buffers.

The 64-bit Host build reports Channel 1,120 bytes, Protocol 3,328 bytes,
BookSession 2,744 bytes and UI controller 104 bytes. Channel/Protocol live in
the Platform allocation; BookSession/controller live only with the foreground
app. These are Host `sizeof` measurements, not ESP32 runtime heap readings.
The existing 4 KiB CLI stack, 512-byte RX ring, 512-byte native TX ring,
2 KiB pending TX buffer and shared 15,000-byte canvas are reused. Each binary
poll processes at most 2,048 input bytes and one complete request.

```bash
bash tools/test-usb-manager.sh
ZECTRIX_USB_SANITIZE=1 bash tools/test-usb-manager.sh
bash tools/test-host.sh
mkdir -p build-usb-previews
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-usb-previews" bash tools/test-display-service.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

Tests cover every payload length and control byte, malformed/oversized frames,
duplicate/stale IDs, partial-frame timeout, TX failure isolation, queued and
executing cancellation, storage exclusion, interrupted/no-overwrite uploads,
exact offsets and counts, download cleanup, paged listing, setting validation
and save-failure retry, button semantics and refresh coalescing. A real pyserial
client runs against the production CLI/protocol/storage over a PTY, including
the shipped command-line entry point and uncertain-mutation errors. A 131,096
byte file was compared byte-for-byte in both directions; the initial round trip
took 3.32 seconds in the Host simulator. This is not USB/SPIFFS throughput.

All 35 Host targets, USB ASan/UBSan (including seven Python client/integration
scenarios), Chinese/English display composition, ShellCheck and three ESP32-S3
firmware configurations passed during the iteration:

| Build | Application bytes | Static internal RAM bytes |
| --- | ---: | ---: |
| Full | 3,027,232 | 213,575 |
| Minimal | 563,328 | 118,731 |
| USB + Chinese UI, Reader/Connectivity/Wi-Fi off | 666,896 | 134,203 |

The existing Full/Minimal comparison passed; Minimal is 81.4% smaller and
excludes the host component and application sources. The independent USB build
includes book storage and excludes Reader, Bluetooth and Wi-Fi components.
Static RAM follows ESP-IDF's IRAM/DRAM/DIRAM report and excludes dynamic heap.

The connected ESP32-S3 passed Full firmware flash/boot smoke, including 8 MiB
PSRAM, partitions, first Launcher frame, boot confirmation, 13 applications
and USB CLI. The runtime-start observation had 92,655 free internal heap bytes;
it is not a USB-transfer peak-memory measurement. Physical transfer speed,
button latency during flash/display BUSY, OS-specific port reopen behavior and
sleep/wake reconnect still require device qualification. The delivery is tracked
in [ASTRA_TASKS.md](../ASTRA_TASKS.md).

[cp-usb]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/network/UsbDriveActivity.cpp
[cp-handoff]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/platform/UsbSerialJtagHandoff.cpp
[cp-web]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp
[flipper-rpc]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/rpc/rpc.c
[flipper-vcp]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/cli/cli_vcp.c
