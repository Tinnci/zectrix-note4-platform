# Local Wi-Fi book transfer

L1.3 adds **SEND BOOKS** to the Launcher. A phone or computer can upload,
download and delete TXT/EPUB files through a local web page. No companion app
or internet connection is required.

## Send books

1. Initialize the content partition once with the explicit `books-flash`
   installation in [READER.md](READER.md#install-books). Existing reader
   libraries are ready to use. Normal firmware flash preserves them.
2. Open **SEND BOOKS** on Note4. Use UP/DOWN to select a mode, then press OK.
3. In **CREATE NOTE4 HOTSPOT**, connect your phone or computer to the displayed
   `NOTE4-XXXX` network. Use the 12-character access code as its Wi-Fi password.
   Keep the connection if the phone reports that this network has no internet.
4. In **USE SAVED HOME WI-FI**, connect your phone or computer to the same
   network as Note4. This mode uses credentials already provisioned through
   Connectivity. Use hotspot mode if no network has been saved.
5. Open the displayed `http://` address in a browser. Enter the access code
   from the Note4 screen.
6. Drop TXT/EPUB files into the page, or choose files on your device. Select
   **Upload & finish**. Files upload in order. The page confirms completion
   before Wi-Fi turns off.
7. Press OK on Note4 to open **BOOK READER**.

The web library supports download and deletion with confirmation. **Refresh**
reloads the library and storage space. **Cancel upload** stops the current
request. Files already saved remain available. A failed batch stops at its
first error and retains the unsent selection for retry. **Finish session**
ends a management session without uploading.

OK on an active Note4 transfer screen stops the session. Hold OK cancels and
returns to the mode menu. Hold OK again returns to the Launcher parent. Hold
DOWN retains global shutdown. The status bar remains active during transfer.
Scene changes request Quality refresh. Progress changes request Fast at most once per second when
the displayed 10-percent step or saved-book count changes.

The completed-session Reader action replaces Send Books after its exit cleanup.
Leaving Library returns to the original Home focus without restarting the old
transfer session. See [NAVIGATION.md](NAVIGATION.md) for shared controls.

Companion BLE synchronization remains available during transfer. The
[radio arbiter](RADIO_ARBITER.md) spaces new outbound durable frames by 250 ms
during active Wi-Fi work, while control replies and retries continue. Normal
sync cadence returns after 500 ms of HTTP inactivity or immediately after Wi-Fi
is released. STA modem sleep and automatic session shutdown limit radio use.

## Session and storage limits

The hotspot uses WPA2 and accepts up to two clients. Each session has a new
access code. Station and hotspot modes run separately. Home-network client
isolation can prevent a browser from reaching Note4. Use hotspot mode in that
case.

| Limit | Behavior |
| --- | --- |
| Power | External power or a valid battery reading of at least 20 percent is required. |
| Policy | Offline and phone-only modes disable book transfer. |
| Startup | A network address must become available within 20 seconds. |
| Idle | Stop after 3 minutes without an authenticated operation. |
| After an upload | Stop after 30 seconds without further authenticated activity. An active file operation can finish within its own deadline. |
| Whole session | Stop after 15 minutes, including an active upload. |
| File request | 120-second deadline, with a 1-second socket read/write timeout. |
| Successful batch | The browser finishes the session immediately. Firmware allows 500 ms for the final reply before shutdown. |
| Filename | Valid UTF-8, at most 63 bytes, `.txt` or `.epub`, without separators or control characters. |
| Capacity | Upload admission leaves 25 percent of the reported SPIFFS capacity for metadata and garbage collection. The page shows the remaining upload allowance. |
| Library | The web page loads 32 entries per request and can manage all eligible files. The reader shows the first 32 names in byte order. |

The server uses the existing 4 MiB `books` SPIFFS partition. It does not change
NVS, OTA slots or the partition table. A new file is written in 1 KiB chunks to
a reserved staging file. Storage checks the declared length, flushes and syncs
the file, closes it, then renames it into the library. No complete book is
buffered in RAM. Existing names are rejected without overwriting them.

Cancellation and failed writes remove the staging file. The next session also
removes staging left by an interrupted boot. An unsuccessful filesystem mount
never formats the partition. A new or damaged partition displays a storage
error. Use the explicit USB installation procedure when initialization or
replacement is intended.

Deleting a file preserves its recent reading position. Give a different edition
a new filename, even if its byte length matches the old file. Reader identity
uses filename and length, as described in [READER.md](READER.md#saved-positions-and-phone-sync).
Upload accepts file bytes and an eligible filename. The reader validates TXT or
EPUB content when opening it.

## Ownership and HTTP API

Connectivity owns the Wi-Fi driver, server and session state machine. The
existing session worker starts and polls the radio. ESP-IDF supplies one HTTP
task with an 8192-byte stack and up to four client sockets. The application
uses the existing SceneManager for Mode → Session and requests renders through
DisplayService.

Storage grants an exclusive management lease after reader files close.
Resource requests, Wi-Fi reconfiguration and RF scans cannot acquire the same
radio during transfer. Shutdown cancels HTTP work and interrupts connected
sockets, joins the HTTP task, stops/deinitializes Wi-Fi, then releases the
Storage lease. A failed stop retains ownership for retry. BLE remains under
its existing owner.

`GET /` serves the self-contained web page. All API operations require
`Authorization: Bearer <screen-code>`. The code is held in browser memory and
is not sent in a URL, cookie or log. Responses use `no-store`, restrictive CSP
and no permissive CORS headers. HTTP is intended for the local hotspot or a
trusted home network. Station mode does not encrypt HTTP traffic.

| Method and path | Operation |
| --- | --- |
| `GET /api/books` | List names, lengths and storage capacity. |
| `GET /api/books?after=<encoded-name>` | Get the next page in filename order. |
| `PUT /api/books/<encoded-name>` | Upload raw bytes with `Content-Length`. |
| `GET /api/books/<encoded-name>` | Download bytes as an attachment. |
| `DELETE /api/books/<encoded-name>` | Delete one book, with an empty body. |
| `POST /api/finish` | End the session, with an empty body. |

Names use UTF-8 percent encoding. Responses distinguish unauthorized (`401`),
invalid filename (`400`), missing file (`404`), existing name or busy (`409`),
interrupted/timed-out upload (`408`), oversized request (`413`), ended session
(`503`) and insufficient storage (`507`). File operations are serialized.

## Development and verification

Run the full Host suite or focused sanitizer checks:

```bash
bash tools/test-host.sh
ZECTRIX_TRANSFER_SANITIZE=1 bash tools/test-book-transfer.sh
```

For interactive browser testing, use a disposable copy of your books:

```bash
book_test_dir=$(mktemp -d)
cp books/*.txt "$book_test_dir/"
bash tools/run-book-web-host.sh "$book_test_dir"
```

The runner prints a loopback URL and uses test code `ABCDEFGH2345`. It shares
the firmware API, Storage code and timeout behavior. Browser deletion changes
the supplied directory. Restart the runner for a fresh session. It requires a
C++17 compiler. Automated socket tests require Bun 1.3.14.

Tests exercise streamed binary transfer, authorization, filename validation,
pagination, storage isolation, interrupted uploads, stop retries and timeout
wraparound. The real ESP HTTP adapter is also compiled with Host API stubs to
verify cancellation while headers or upload bodies are blocked. Real socket
tests exercise the HTTP protocol and session shutdown. Display tests render
mode, progress, completion and error previews, preserving the status viewport
and retrying failed display commits.

The L1.3 iteration passed all 29 Host targets, focused ASan/UBSan checks,
ShellCheck and the ESP32-S3 build. A real browser exercised login, download,
delete confirmation, drag-and-drop upload and completion on the Host server.
Desktop, mobile and device UI previews were inspected. Connected-device flash
and boot smoke passed. AP/STA file transfer, physical button operation, radio
current and BLE/Wi-Fi coexistence were not exercised on hardware.

CrossPoint's [web server](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp)
informs temporary AP/STA sessions, streamed writes, partial-file cleanup and
radio shutdown. Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
informs the mode/session hierarchy and Back propagation. The implementation
uses existing Note4 service boundaries. No upstream application code is copied.
L1.4 adds the [ambient sleep cover](SLEEP_COVER.md). Global shutdown stops the
transfer and radio before capturing and rendering the final cover.
