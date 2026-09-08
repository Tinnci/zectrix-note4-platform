# Connectivity platform contract

Status: C1 design baseline. Protocol version: 1.0.

## Ownership

`SyncEngine` owns durable revisions, outbox state, receive cursors and command
dedupe records. `ConnectivityPolicy` selects a path. `CompanionProtocol` owns
wire validation and message semantics. `BleLink` and `WifiLink` own transport
lifecycle. `PowerService` remains the owner of product power transitions.

An application can request a product operation. It cannot select a GATT
characteristic, connection handle, Wi-Fi mode, task, queue or socket.

The first semantic boundary is internal and draft. `ResourceRequest` describes
a capability, expected bounded size, deadline and durability. Policy consumes
phone presence, user mode, product power state, external power, battery,
credential availability and bounded retry delays. It returns one of
`PhoneProxy`, `DirectWifi` or `Defer` with a stable reason. This does not add or
change an SDK 1.0 header.

Automatic policy prefers the connected phone for responses up to 2048 bytes.
It can select Wi-Fi for larger data, urgent work, phone absence or phone
backoff. Wi-Fi requires stored credentials and either external power or at
least 20 percent battery. Phone-only, Wi-Fi-only and offline user modes are
strict. A shutdown state always defers new work.

## Protocol limits

Protocol 1.0 uses these hard limits:

| Item | Limit |
| --- | ---: |
| Frame header | 24 bytes |
| Reassembled payload | 4096 bytes |
| TLV field | 2048 bytes |
| Stream chunk payload | 512 bytes |
| In-flight confirmed frames per direction | 1 |
| Durable outbox entries in the first implementation | 8 |
| Command dedupe records | 16 |

Every length is checked before allocation, copy or field access. A limit can
increase only through a negotiated compatible version and measured memory
evidence.

The frame header is little-endian:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | magic `0x435a` (`5a 43` on wire) |
| 2 | 1 | protocol major |
| 3 | 1 | protocol minor |
| 4 | 1 | message class |
| 5 | 1 | flags |
| 6 | 2 | message type |
| 8 | 4 | request ID |
| 12 | 4 | sequence |
| 16 | 2 | payload length |
| 18 | 2 | reserved, zero in version 1 |
| 20 | 4 | IEEE CRC-32 of bytes 0–19 followed by the payload |

The transport fragment header is eight bytes: magic `0xa7`, fragment version
1, start/end flags, one zero reserved byte, frame ID and byte offset. A peer
accepts only the exact next offset. The normative examples are in
`protocol/golden-vectors.json`.

## Version rules

- Peers must have the same protocol major version.
- A lower minor version selects the common feature set.
- A peer ignores an unknown optional TLV field.
- A peer rejects an unknown required message type.
- A downgrade cannot bypass pairing, authorization or a required capability.
- Version mismatch is a typed terminal session error, not a retry loop.

## Delivery rules

- `sequence` orders frames in one protocol session.
- `request_id` identifies a command or resource operation across reconnects.
- ACK confirms accepted ownership, not only receipt of BLE bytes.
- NACK contains a stable error code and can contain a bounded retry delay.
- Timeout returns the operation to retry, defer or unknown-outcome policy.
- Durable state uses a monotonically increasing revision per state key.
- A repeated durable revision is acknowledged without applying it twice.
- A repeated idempotent command returns the stored result.
- A non-idempotent command with an unknown outcome is not retried silently.

### Durable sync persistence

The first engine tracks at most eight durable keys. Each pending value is at
most 256 bytes. It retains 16 command results with at most 32 response bytes
for duplicate suppression. These are sync-state limits, not the larger wire
frame limits.

The firmware record starts with `ZSYN`, format version 2, a 16-byte header,
generation and body length. The body contains bounded outbox, inbox, bidirectional
cursor and command-result records. Each inbox value and its receive cursor are
saved together. An IEEE CRC-32 closes the record. Format 1 records remain readable;
pending keys acquire reserved cursor slots when loaded. Loading
rejects an unknown format, duplicate key or request ID, zero identifier,
invalid record length, oversized value, nonzero reserved field or CRC failure.

An enqueue, ACK, receive-cursor commit or command result is successful only
after `SyncStore::Save` succeeds. An unsuccessful save restores the prior
in-memory state. A corrupt record produces a diagnostic recovery result and an
empty controlled resynchronization state. It is never partially applied.

The version 2 buffer is bounded at 5376 bytes, including up to eight received
values as well as eight pending values. The same eight-key set bounds both
directions. `ConnectivityService::PutDurableState` persists submissions through
Storage's `comp_sync` blob. `ReadDurableState` returns a copy of the latest received
revision and value for idempotent application by the product owner.

Android stores the corresponding queue, received values and cursors in one
bounded record per Note4, under the app's private, non-backed-up files directory.
Version 1 queue files remain readable. Writes sync a temporary file, atomically
replace the committed file and sync its directory; they never delete the previous
record before replacement. Invalid files are reported without partially loading
or overwriting them.

### Cursor exchange and durable replay

Hello and accepted, authorized HelloAck require TLV type `4` (sync cursors).
Its value starts with version `1`, an 8-bit count and two zero reserved bytes.
Each of at most eight records contains `key:uint16`,
`outbound_acknowledged:uint32`, `inbound_applied:uint32` and
`pending_revision:uint32`, all little-endian. Zero pending revision means no
pending value; otherwise it must exceed the acknowledged revision. Keys are
nonzero and unique. The field negotiates this bounded sync capability; a peer
without it cannot be declared synchronized.

Reconciliation validates both directions before changing any cursor. A peer's
receive cursor can retire a lost ACK, including an older revision whose newer
value is still pending. It cannot exceed the latest known revision or regress
below a previously acknowledged revision. Such a mismatch reports resynchronization
required; acknowledged payloads have already been retired, so the engine cannot
silently reconstruct them. Recovery requires the product owner to restore its
state, or an explicit local reset of the peer relationship. Forgetting a peer
clears its durable state before removing its protocol identity.

Durable-state message type `1` carries required TLVs `1` (two-byte key), `2`
(four-byte revision), and `3` (0–256 value bytes). Requests have flags `0x05`,
nonzero sequence and a request ID equal to that sequence. Control type `3` is
the durable ACK/NACK: response flag `0x02`, matching request ID and sequence,
and required TLVs `1`, `2`, `4` (one-byte result: accepted `0`, store error `1`,
invalid/conflicting state `2`, capacity exhausted `3`). Unknown optional fields
are skipped within the 274-byte durable payload bound; duplicate known fields
and unknown required fields are rejected.

Each direction permits one confirmed frame in flight. An ACK must match the
in-flight sequence, key and revision. Retries reuse the original frame even when
a newer value replaces its outbox entry. There are at most three sends, three
seconds apart, and a 15-second progress timeout. Failure ends the session without
discarding its outbox. Reconnect starts fresh session sequences and exchanges
persisted per-key cursors; BLE byte receipt alone never advances a cursor.

The receiver saves the value and cursor before replying, and duplicate revisions
do not repeat the save. A conflicting payload at the same retained revision is
rejected. Android reaches `READY`, and firmware permits phone resource requests,
only after both advertised replay windows converge. The existing session owner
handles persistence, replay and resource scheduling; no additional firmware task
is introduced. Host and Android tests exercise both directions, lost ACKs,
restart, coalescing, bounded retries, corruption, capacity and failed saves.

### Q1.3 durable protocol cross-inspection

[Pebble AppMessage](https://developer.rebble.io/docs/c/Foundation/AppMessage/)
provides the reference for symmetric inbox/outbox messages, bounded buffers,
busy responses and explicit ACK/NACK or timeout outcomes. Its successful
asynchronous send call accepts work; delivery is reported later by a callback.
Note4's durable ACK additionally requires persistence of the received value
and cursor before the reply can be sent.

While TX is busy, a duplicate frame preserves the queued reply, including a
NACK after a failed save. It cannot trigger another save and turn that pending
failure into success. Only committed new state or acknowledgement of an
in-flight value advances replay progress. Duplicate traffic cannot postpone
the 15-second progress timeout. An ACK for a frame that has not been sent
cannot retire its pending value. Optional fields count toward the full
274-byte durable payload limit for both states and ACKs.

The C++ and Kotlin tests cut maximum-size state frames and their ACKs at every
20-byte ATT packet boundary in both directions. Each scenario recreates the
volatile owners from their stores, exchanges cursors and verifies convergence
with a newer coalesced value. The tests also interrupt receive and sender ACK
commits through failed saves, preserving the last committed record. Additional
cases cover queued NACK retries, duplicate traffic during stalled replay and
ACK size limits. These simulations validate application protocol ownership and
recovery; physical BLE disconnect timing still requires device qualification.

## Resource gateway

The first resource capability is `public_test_document_v1`. The phone owns the
HTTPS endpoint and enforces the response content type, 2048-byte body limit and
timeout. Firmware sends no URL or credential. Android maps this capability to
`https://zectrix.com/robots.txt`, uses `GET`, rejects redirects and accepts only
a non-empty, valid UTF-8 `text/plain` response within the requested bound.
Stable result classes are:

- success;
- phone unavailable;
- phone offline;
- timeout;
- server error;
- response too large;
- invalid response;
- not authorized;
- unsupported capability.

The request uses command message type `0x0100`. Required TLVs carry capability,
maximum response bytes, timeout and the durable flag; cache maximum age is an
optional TLV. A response repeats the request ID and sequence with the response
flag. Only an authorized current protocol session can issue or service the
operation.

Android retains at most 16 request IDs. An identical completed terminal request
returns its stored result without another HTTPS call. Reusing an ID with a
different payload is invalid. Offline, unavailable and timeout results are
transient: a durable firmware request retains its ID across reconnect and uses
the bounded retry delay. All other results are terminal. The normative complete
request frame, including CRC, is in `protocol/golden-vectors.json`.

The direct Wi-Fi backend can implement the same resource capability. This
keeps the application semantic result independent of the selected transport.

The C1.7 host-qualified backend is a bounded, poll-driven burst state machine.
It loads credentials through a Storage-owned source, then exposes explicit
station start, association, IP, DNS, TLS, transfer and stop phases through an
internal driver seam. No URL, credential, socket, `esp_wifi`, `esp_netif` or
lwIP type crosses that seam into an application or SDK header. The first
capability remains `public_test_document_v1` with a maximum 2048-byte result.

Every operation result is published only after the backend has attempted to
stop the station. Stop has an independent two-second bound; a failed stop is
reported separately and makes that backend instance non-reusable. Credentials
are held in a fixed-size temporary copy and cleared after station start. Host
fakes cover success, unavailable/invalid credentials, authentication, IP, DNS,
TLS, timeout, transfer, size, malformed response, cancellation, unsupported
capability and stop failure. The ESP-IDF driver is implemented. Real association,
resource transfer and power-down measurements remain hardware exit work.

### Direct HTTPS execution (C1.2)

`ConnectivityService` runs `ResourceClient` on its existing session owner. A
connected, authorized phone remains preferred. Phone absence, send failure,
disconnect, offline status or timeout can select direct Wi-Fi through
`ConnectivityPolicy`. Terminal resource errors do not trigger another fetch.
Phone-only, Wi-Fi-only, offline, shutdown and minimum battery rules apply to
selection and to policy changes during a transfer.

Each selected transport attempt has the requested timeout. A durable request
keeps its ID and payload for in-memory retries and BLE reconnects, with a
one-second to five-minute backoff. Retry notices are advisory: an unread notice
does not pause background retries, and a newly available path can resume work.
A terminal response stays available until consumed. Reboot persistence and
cursor replay belong to the subsequent sync work.

The ESP driver uses asynchronous DNS and verified TLS, then `esp_http_client`
over that TLS stream for `GET https://zectrix.com/robots.txt`. It validates the
CA chain, hostname and certificate dates. The bounded reader accepts a
non-empty UTF-8 `text/plain` body with fixed-length, chunked or connection-close
framing. It rejects redirects, compressed content, conflicting lengths,
truncation and excess data. Each poll reads at most 512 wire bytes; headers and
chunk metadata share a 4096-byte bound, with a 512-byte line limit.

Stored station credentials are provisioned through
`ConnectivityService::ConfigureWifi`; they can also unblock an already deferred
request. `SetUserPolicy` persists the selected mode. Applications publish copied
power samples through `UpdatePower`. The RF diagnostic uses the same driver's
exclusive radio claim, so a scan and a resource burst cannot own Wi-Fi at once.
Success, failure, timeout and cancellation all use the station stop path before
publishing a direct result. The result carries transfer and stop status
separately. Product shutdown stops connectivity before the power transition.

TLS requires a configured UTC system clock. `TimeService` can initialize it from
a valid RTC reading and an explicit UTC offset; RTC calendar fields retain their
local-time meaning. At boot the application uses the Storage setting
`rtc_utc_offset` (signed seconds east of UTC) when present. Provisioning must set
the correct RTC and offset, or synchronize the UTC clock through the time owner,
before a direct fetch. Certificate validation is never bypassed to accommodate
an unset clock. Firmware defaults enable the certificate bundle and date checks.

`tools/test-resource-client.sh` exercises selection, escalation, retry,
cancellation and stop outcomes with fake transports. `tools/test-wifi-http.sh`
exercises the production response reader and custom transport callbacks with
an HTTP API fake, including partial writes and reads. The ESP-IDF firmware build
checks the real driver and HTTP API integration. Radio current, real TLS latency
and physical BLE/Wi-Fi coexistence still require a Note4.

The ESP driver has one calling owner; callbacks publish link flags through
atomics. Event handler unregistration synchronizes with callbacks on the
ESP-IDF event-loop mutex before interface or driver storage is released. An
uncancellable DNS query owns a separate callback reference, so a reply after
timeout or driver destruction cannot access the retired driver. A new burst
uses a separate query context. A failed station stop retains the exclusive
radio claim and callback storage until cleanup succeeds.

Q1.1 extends `tools/test-wifi-backend.sh` to compile the production ESP driver
with event, netif and DNS fakes. It covers concurrent event publication,
unregistration during a callback, partial-start cleanup, exclusive radio use,
stop retry, cancellation before and after DNS submission, cached/failed
lookups and a previous burst's late reply. The HTTP seam in this test is a
stub; production HTTP framing and transport remain covered by
`tools/test-wifi-http.sh`. The backend target accepts `CXX` for sanitizer
compiler wrappers.

Q1.2 exercises complete backend bursts through the production ESP driver with
a scripted HTTP seam. Success, transfer errors, invalid responses, timeout and
cancellation close HTTP and TLS before stopping/deinitializing Wi-Fi, removing
event handlers and destroying the station interface. The terminal outcome is
unavailable while cleanup is pending and reports any cleanup failure separately.
A subsequent RF scan can acquire Wi-Fi only after successful release.
Stop, deinit and handler-unregistration
failures retain the interface and exclusive claim for cleanup retry; they do
not make a faulted backend reusable.

Station cleanup leaves the shared event loop and network core available. It
does not stop the BLE owner or directly disable the shared PHY; ESP-IDF owns
Wi-Fi/BLE coexistence. Product shutdown stops connectivity before releasing
NFC, display and board resources through `Platform::Shutdown()`. Host resource
counts establish software ownership and cleanup, not physical modem current
or BLE link quality under RF contention.

## Security lifecycle

Pairing requires a local Note4 action. The firmware requests bonding, LE Secure
Connections and authenticated passkey entry. The Note4 displays the passkey.
Bond keys stay in the Bluetooth stack's persistent store. Connectivity stores
only protocol peer identity and authorization state through `StorageService`.

Bond deletion, lost-phone recovery and new-phone migration require an explicit
local reset action. A new link is not authorized only because it can decode a
protocol Hello. Resource responses and commands are accepted only after link
security, protocol negotiation and peer authorization succeed.

### Readiness gates

The implementation must not collapse these states:

| Gate | Evidence | Meaning |
| --- | --- | --- |
| Associated | Android Companion Device Manager record | The user approved an app-device relationship. This is not a Bluetooth bond. |
| Link secure | encrypted, authenticated and bonded connection | The current BLE link has stack-level security. |
| Transport ready | secure link and notification subscription | Both GATT directions are available. |
| Protocol negotiated | authenticated Hello write and matching HelloAck | Both peers accept protocol 1.0 framing. |
| Peer authorized | stored protocol identity and authorization decision | The peer may exchange durable state. |
| Synchronized | validated receive cursors and both replay windows converged | New phone operations can run. |

Protocol 1.0 reserves control message type `1` for Hello and type `2` for
HelloAck. Hello carries exactly one of these identity TLVs, plus sync cursors:

- `kHelloEnrollmentProofType = 1`: generation `uint32 LE`, 16-byte
  enrollment token and 16-byte companion identity. The firmware consumes the
  token and persists the companion identity after successful validation.
- `kHelloCompanionIdentityType = 2`: 16-byte companion identity for
  reconnect authorization after a completed enrollment.

HelloAck has the response flag and repeats the request ID and sequence. Its
payload carries `kHelloAckStatusType = 3`: status byte (`0` accepted, `1`
rejected), flags byte (`0x01` peer authorized) and error reason `uint16 LE`.
The exchange proves the current link and protocol path. A successful status
does not authorize a peer unless the peer-authorized flag is also set.

The proof is consumed only under the current encrypted, authenticated, bonded
and subscribed transport session. Identity persistence must succeed before that
session becomes authorized. A failed save leaves the token consumed and returns
store error `9`; a new NFC tap supplies fresh material. Proof plus reconnect
identity in the same Hello is rejected before either has side effects. A different
stored identity requires the existing local forget-peer action. Missing sync
capability is error `10`; inconsistent cursors are error `11`.

Android commits its Keystore-protected identity before submitting a proof, and
discards the proof after its first send. If enrollment succeeded but HelloAck was
lost, reconnect presents that persisted identity instead of replaying the token.
Rejected, malformed, unauthorized or cursor-less HelloAck cannot enter `READY`.

The Android application serializes all GATT writes and completes MTU setup
before Hello. It reaches its connected state only after a matching accepted
HelloAck. The firmware services Hello in the background through
`ConnectivityService`; the internal FreeRTOS task is an implementation
mechanism, not an application-facing lifecycle or architecture boundary.

The firmware state `ProtocolNegotiatedLocal` means that it accepted Hello and
started HelloAck transport for the same BLE session. It does not prove that Android received
HelloAck. Android enters `READY` only after it receives and validates the
matching response. Product readiness additionally requires peer authorization
and synchronization convergence.

Reconnect advertising rejects an unknown peer unless a local pairing window
is active. This prevents an untrusted central from occupying the connection
for a full security timeout. Pairing failure and automatic reconnect use
explicit states and bounded retry delay.

### Diagnostic events

Firmware and Android logs use structured `event=... session=...` records.
Required events cover connection, security start/result, subscription,
Hello/HelloAck readiness, pairing-window open/expiry, rejection and
disconnect. Logs must not contain a passkey, bond key, protocol credential or
raw device address. A session ID is process-local diagnostic correlation; it
is not a peer identity.

## Recovery

After BLE loss or process/device reboot:

1. establish and secure the link;
2. exchange Hello, capabilities and receive cursors;
3. resend unacknowledged durable revisions;
4. resume idempotent requests that policy permits;
5. report non-idempotent unknown outcomes;
6. continue new work only after the replay window converges.

Corrupt persistent state is rejected. The engine reports a diagnostic error
and starts a controlled resynchronization. It does not use unchecked bytes.

## NFC-assisted companion enrollment

NFC can authorize one companion enrollment. It does not replace Bluetooth link
security and it never carries a bond key, LTK or long-term device secret.

The first implementation has two stages:

1. A prepared NDEF record identifies the Note4 and its BLE peripheral role. An
   NFC field event opens a short local pairing window. Existing authenticated
   passkey pairing remains available.
2. A versioned Zectrix MIME record carries a 128-bit random enrollment token
   and generation. Android returns that token in the protocol Hello after the
   BLE link is encrypted, authenticated, bonded and subscribed. The firmware
   binds it to the current enrollment generation and BLE session, marks the
   companion identity as authorized, and consumes the token.

The token is generated with the platform cryptographic random source. It is
single-use, short-lived, RAM-only and invalid after restart. It is not a peer
identity. A successful enrollment stores a separate companion identity through
the Storage owner. A later reconnect requires the stored BLE bond and companion
identity; it does not require another NFC tap.

The NDEF payload is written before an RF field is present. The field callback
only submits a bounded bootstrap event. It does not perform I2C, BLE or protocol
work and it does not call `BleLink` directly. This prevents I2C activity from
interrupting an RF transfer and preserves execution ownership.

The ownership path is:

```text
ZectrixNfc board driver
        |
   internal NfcService
        |
 PairingBootstrap
   |           |
   |           +-- Companion session authorization
   +-------------- ConnectivityService pairing request
```

Standard `application/vnd.bluetooth.le.oob` carrier records and LE Secure
Connections OOB C/R data are a later compatibility-gated enhancement. ESP-IDF
5.5.2 NimBLE contains the required SC-OOB primitives, but Android public app
APIs and OEM NFC handover behavior are not uniform. The passkey path remains
the qualified fallback until a device matrix proves the standard OOB path.

## Required evidence

Software evidence:

- frame and TLV encode/decode tests;
- invalid, truncated, oversized and CRC mutation tests;
- version and capability negotiation tests;
- reconnect, replay, duplicate, out-of-order and timeout tests;
- durable queue recovery tests;
- policy matrix tests;
- fake BLE and Wi-Fi link tests;
- C++/Kotlin golden-vector agreement;
- architecture checker and clean builds.

Hardware evidence:

- fresh pair and explicit unpair/re-pair;
- reconnect after BLE loss, Note4 reboot and Android lifecycle events;
- durable state, command/reply and real HTTPS resource request;
- offline queue replay and malformed-message rejection;
- direct Wi-Fi success and fallback decisions;
- standby, sleep, wake, shutdown and reconnect soak;
- Note4 power, latency, heap and stack-watermark measurements.
