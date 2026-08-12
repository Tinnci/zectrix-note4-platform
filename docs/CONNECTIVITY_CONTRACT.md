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

### Sync persistence format 1

The first engine tracks at most eight durable keys. Each pending value is at
most 256 bytes. It retains 16 command results with at most 32 response bytes
for duplicate suppression. These are sync-state limits, not the larger wire
frame limits.

The persistent record starts with `ZSYN`, format version 1, a 16-byte header,
generation and body length. The body contains bounded outbox, bidirectional
cursor and command-result records. An IEEE CRC-32 closes the record. Loading
rejects an unknown format, duplicate key or request ID, zero identifier,
invalid record length, oversized value, nonzero reserved field or CRC failure.

An enqueue, ACK, receive-cursor commit or command result is successful only
after `SyncStore::Save` succeeds. An unsuccessful save restores the prior
in-memory state. A corrupt record produces a diagnostic recovery result and an
empty controlled resynchronization state. It is never partially applied.

## Resource gateway

The first resource capability is `public_test_document_v1`. The phone owns the
HTTPS endpoint and enforces the response content type, 2048-byte body limit and
timeout. Firmware sends no URL or credential. Stable result classes are:

- success;
- phone unavailable;
- phone offline;
- timeout;
- server error;
- response too large;
- invalid response;
- not authorized;
- unsupported capability.

The direct Wi-Fi backend can implement the same resource capability. This
keeps the application semantic result independent of the selected transport.

## Security lifecycle

Pairing requires a local Note4 action. The firmware requests bonding, LE Secure
Connections and authenticated passkey entry. The Note4 displays the passkey.
Bond keys stay in the Bluetooth stack's persistent store. Connectivity stores
only protocol peer identity and authorization state through `StorageService`.

Bond deletion, lost-phone recovery and new-phone migration require an explicit
local reset action. A new link is not authorized only because it can decode a
protocol Hello. Resource responses and commands are accepted only after link
security, protocol negotiation and peer authorization succeed.

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
