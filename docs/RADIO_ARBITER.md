# Wi-Fi / BLE radio arbitration

E1.5 coordinates temporary Wi-Fi work with durable Companion synchronization on
the ESP32-S3. `RadioArbiter` schedules work on the existing Connectivity session
owner. Its state shares the resource mutex with foreground start/stop requests.
It has two scalar fields, allocates no memory and
creates no task. `ConnectivitySnapshot::radio_mode` reports its current mode.

## Scheduling

| Mode | Evidence | New device-to-phone durable frames |
| --- | --- | --- |
| Companion | Wi-Fi is released and no Wi-Fi operation is pending. | Normal sync cadence. |
| Wi-Fi burst | Resource startup/HTTPS, book startup, an active HTTP operation, recent HTTP activity, or an external RF diagnostic claim. | One admission per 250 ms. |
| Shared idle | A book server is available, with no active authenticated operation and at least 500 ms without activity. | Normal sync cadence. |
| Wi-Fi stopping | Resource or book cleanup is pending or has failed. | One admission per 250 ms until cleanup completes. |

Entering a burst defers new durable work for 250 ms. Each admitted frame starts
the next interval. Continuing HTTP activity and transitions from burst to
cleanup preserve that deadline, so Wi-Fi cannot indefinitely postpone sync.
The server publishes activity for both uploads and downloads. Its copied
active flag also covers slow clients waiting for their next chunk. The 500 ms
quiet period prevents rapid mode changes between nearby requests. A successful
Wi-Fi stop restores normal sync immediately.

`SyncSession::Poll` reports admission of a new durable frame independently of
transport acceptance. Once admitted, that frame can finish when BLE becomes
available. Its existing 3-second retries and retry/progress limits continue to
apply. Inbound durable data is persisted and ACK/NACK responses bypass pacing.
The Companion remains free to initiate its own inbound synchronization under
the existing one-frame confirmation window; this scheduler controls outbound
admission, not inbound airtime.

Pairing, security, Hello/HelloAck, NFC authorization and BLE fragment completion
continue on their existing paths. A phone resource request retains exclusive
ownership of its confirmation window. Arbitration never authorizes a peer or
retires durable state; disconnect/reconnect uses the existing persisted cursors.

The session waiter takes the earliest resource, sync or admission deadline.
Pending control replies and retries retain their deadlines. Converged and
disconnected sessions add no periodic wakeups. Timing uses bounded unsigned
millisecond differences, including timer wraparound and HTTP activity published
just after a session poll captured its time. The 250 ms interval is an application
admission policy; actual delivery also depends on session scheduling, BLE
backpressure, flash operations and RF conditions.

## Radio ownership and power

The ESP driver's existing atomic claim remains the authority for exclusive
Wi-Fi ownership across HTTPS, AP/STA book sessions and diagnostic scans.
`RadioClaimed()` only observes it. A failed stop, deinitialization or callback
unregistration retains the claim and callback storage. StopFailed is explicitly
recognized even though a failed backend no longer reports `Busy()`.

Power and user policy continue to cancel prohibited work on the next owner
poll. HTTP handlers join before Wi-Fi teardown and the Storage lease release.
Book completion/cancellation/timeouts and resource completion still stop and
deinitialize Wi-Fi. Global shutdown joins the session owner before stopping BLE.

Every STA initialization, including diagnostics, explicitly selects
`WIFI_PS_MIN_MODEM`. AP sessions use the AP lifecycle and terminate under the
existing session limits. Defaults enable ESP-IDF software coexistence and BLE
modem sleep, place the BLE controller/host on core 0 and the Wi-Fi stack on core 1.
As with other defaults, an ordinary build retains saved menuconfig choices;
the named Full profile rebuilds its configuration from committed defaults.

ESP-IDF performs physical antenna arbitration automatically for ordinary
Wi-Fi/BLE coexistence. Its deprecated `esp_coex_preference_set` is unsuitable
for a new scheduling dependency, and BLE Mesh status bits describe a different
protocol. See the ESP-IDF 5.5
[ESP32-S3 coexistence guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-guides/coexist.html).

## References and verification

CrossPoint's
[web server](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp)
informs bounded streaming and temporary transfer ownership. Upstream disables
Wi-Fi sleep for its web session; Note4 retains STA modem sleep while pacing
outbound BLE work because both radios share this product's live session.
Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c)
inform single-owner state transitions and copied presentation state. Radio
policy stays in Connectivity, so application navigation cannot stop or recreate
the Companion transport. Reader pagination and static sleep covers retain
their existing foreground ownership; see [NAVIGATION.md](NAVIGATION.md).

Run `bash tools/test-radio-arbiter.sh`, or set `ZECTRIX_RADIO_SANITIZE=1` for
ASan/UBSan. It executes the production arbiter, sync sessions, resource client,
book lifecycle, HTTP API and book storage with simulated transport/time. In
both AP and STA scenarios, a 256 KiB upload uses 256 reads of at most 1 KiB;
eight outbound 256-byte durable states converge in 2000 simulated ms while
bidirectional synchronization and the streamed file continue. Downloads also
publish activity. Tests cover transport backpressure, immediate ACK/NACK,
retry exhaustion, persisted reconnect, phone confirmation ownership,
cancellation, low power, policy changes, failed cleanup and clock wraparound.

`tools/test-wifi-backend.sh` compiles the production ESP driver against IDF
fakes and checks STA power-save setup, initialization failure and claim retention
through cleanup failures. E1.5 passed all 33 Host targets, the focused ASan/UBSan
test, ShellCheck, Full/Minimal/BLE-only ESP32-S3 builds and a connected-device
Full flash/boot smoke. The device reached its first Launcher frame with BLE
reconnect advertising active. Host timing and resource counts establish software behavior;
physical AP/STA throughput, BLE latency and modem/standby current require hardware
measurements.
