# ADR-0004: Use a BLE-first companion protocol with Wi-Fi escalation

Status: Accepted for C1 design on 2026-08-12. Hardware qualification is still
required before C1 can close.

## Context

The Note4 needs low-power background synchronization, controlled access to
internet resources and an independent path for larger or urgent transfers.
ESP32-S3 has one 2.4 GHz radio and supports Bluetooth LE, not Bluetooth
Classic. ESP-IDF 5.5.2 supplies ESP-NimBLE and Wi-Fi/BLE coexistence.

M4 froze a source-stable application SDK. It did not freeze a connectivity
API. Radio handles, callbacks, RTOS objects and network credentials must stay
below the application boundary.

## Decision

Use this dependency direction:

```text
Applications
    |
internal product semantics: state, command and resource request
    |
SyncEngine and ConnectivityPolicy
    |
Zectrix Companion Protocol 1.x
    |                         |
BleLink                  WifiLink
    |                         |
ESP-NimBLE       esp_wifi / esp_netif / TLS
    |
ESP-IDF and IDF FreeRTOS
```

The first connectivity API is internal and explicitly draft. It does not
change SDK 1.0. A later additive SDK minor version needs evidence from a real
application consumer. It must not expose ESP-IDF, FreeRTOS, GAP, GATT, ATT,
SMP, L2CAP, connection handles or Wi-Fi handles.

The Note4 is a BLE Peripheral and GATT Server. The Android companion is a BLE
Central and GATT Client. One custom service has two data characteristics:

- `phone_to_note`: write with response;
- `note_to_phone`: notify, with protocol-level confirmation.

Characteristics are transport lanes. They are not business APIs. A small
fragment header supports MTU-independent reassembly. Only one frame in each
direction can be in flight in protocol 1.0. The sender waits for an ACK, NACK
or timeout before it sends the next confirmed frame. This small window limits
RAM, makes reconnect behavior deterministic and is sufficient for the first
low-rate consumers. A later compatible version can negotiate a larger window.

Protocol 1.0 uses a fixed 24-byte little-endian frame header and a bounded TLV
payload. The header contains magic, protocol version, message class, flags,
message type, request ID, sequence, payload length and CRC-32. The CRC covers
the header fields before the CRC and the payload. Receivers reject an invalid
magic, major version, length, CRC, class, flag combination or field boundary.
Unknown TLV fields are skipped. Unknown required message types get a version
or unsupported-message NACK.

Messages have one of these semantic classes:

- durable state: a revisioned value that must converge;
- command/reply: a bounded operation with a request ID and duplicate record;
- stream/blob: an explicitly opened, chunked and closed transfer;
- control: negotiation, ACK, NACK, flow control and session status.

The durable sender stores its latest unacknowledged revision and payload. The
receiver stores its acknowledged revision. Reconnect starts with a Hello and
cursor exchange. A duplicate durable revision is acknowledged without a
second side effect. A side-effecting command uses its request ID as a dedupe
key. The receiver returns the prior result when that key is still in the
bounded dedupe record. A command that cannot be made idempotent must not be
retried automatically after an unknown outcome.

The phone is the default internet gateway. Firmware sends a capability-based
resource request. It does not send an arbitrary URL, header or credential.
The first resource is a public, versioned test document with no account or
secret. Android maps the capability to an HTTPS endpoint, enforces size and
time limits, and returns a typed status and bounded body. Authentication
tokens and high-value account credentials stay on the phone.

Direct Wi-Fi is an escalation path. `WifiLink` owns STA start, association, IP,
DNS/TLS readiness, disconnect and radio stop. The existing Diagnostics Wi-Fi
scan remains an RF qualification path and is not the product network backend.
Applications do not call `esp_wifi_*`.

`ConnectivityPolicy` selects phone proxy, direct Wi-Fi or defer from phone
presence, request capability and size, deadline, battery/external power,
credential availability, user policy and recent failures. It does not assume
that BLE and Wi-Fi bulk data run concurrently. Coexistence and connection
parameters remain measured policy inputs.

BLE uses LE Secure Connections, bonding and authenticated passkey entry where
the phone platform supports it. Note4 displays a generated six-digit passkey;
the user enters it on the phone. Pairing starts only from an explicit local
action. Link encryption, protocol negotiation and application authorization
are separate checks. Bond reset and phone replacement require an explicit
local action. Logs do not include keys, Wi-Fi passwords, tokens or complete
sensitive payloads.

## Encoding choice

Use a small custom binary envelope and TLV payload for protocol 1.0.

Advantages:

- deterministic allocation limits on ESP32-S3;
- no new protobuf-c, CBOR or code-generator dependency;
- direct C++ and Kotlin implementations;
- byte-exact golden vectors and simple mutation tests;
- unknown fields can be skipped for compatible extension.

Costs:

- the project owns validation and encoder maintenance;
- generic tooling is less available than for protobuf or CBOR;
- schema changes require disciplined message and field registries.

This choice is justified for a small capability set and bounded embedded
messages. Reconsider protobuf or CBOR only when message breadth, third-party
interoperability or schema tooling outweighs its Flash, RAM and build cost.

## Reference decisions

### Pebble AppMessage

Adopt acknowledgement, a phone-side internet proxy and a copied current-state
model. Adjust dictionary messages into typed capability messages. Do not adopt
arbitrary application-controlled HTTP or an unbounded dynamic dictionary.

Reference: <https://developer.rebble.io/developer.pebble.com/guides/communication/using-pebblekit-js/>

### Bangle.js and Gadgetbridge

Adopt the thin two-characteristic UART transport, MTU chunking and phone-owned
HTTP execution. Adjust the JavaScript/JSON command stream into a bounded binary
protocol with explicit version, ACK, dedupe and length rules. Do not execute
device-provided code or arbitrary URLs.

Upstream source:
<https://github.com/Freeyourgadget/Gadgetbridge/blob/master/app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/banglejs/BangleJSDeviceSupport.java>

### Flipper Zero

Adopt the separation between BLE serial transport and RPC, plus explicit flow
control. Do not import its service-thread topology, protobuf dependency or
dynamic application model without a Note4 requirement.

Reference: <https://developer.flipper.net/flipperzero/doxygen/serial__service_8h.html>

### Wear OS Data Layer

Adopt the semantic split between durable data, best-effort messages and larger
streams. Adjust it to a single phone and a small persistent cursor. Do not
claim Wear OS routing, cloud relay or multi-node semantics.

Reference: <https://developer.android.com/training/wearables/data/overview>

### ESP-IDF and Android

Use ESP-NimBLE because ESP32-S3 is BLE-only and the project does not need a
Bluetooth Classic host. Use Companion Device Manager for user-approved
association and `CompanionDeviceService` device-presence support where the
Android version permits it. Do not use fixed periodic background scans as the
primary reconnect mechanism.

References:

- <https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/bluetooth/nimble/index.html>
- <https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-guides/coexist.html>
- <https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing>
- <https://developer.android.com/develop/connectivity/bluetooth/ble/background>

## Power and execution model

Connectivity defines product states, not task topology:

- active;
- BLE connected standby;
- BLE advertising standby;
- deep/offline standby;
- Wi-Fi burst;
- shutdown.

ESP-IDF callbacks copy bounded events into owned state. They do not call an
application while a NimBLE or Wi-Fi callback owns the stack. Start with the
minimum task and queue set required by blocking APIs. Measure stack watermark,
minimum heap, largest block, wake latency and sync latency before adding a
resident task.

ESP-IDF's ESP32-S3 NimBLE power-save example reports development-board values,
but those values are not Note4 qualification evidence. C1 must measure the
real board. Deep sleep does not preserve a BLE or Wi-Fi connection.

## Consequences

- M4 SDK 1.0 remains unchanged.
- A real Android application is part of C1, not an optional sample.
- Protocol and state-machine tests run on the host without ESP-IDF or Android.
- C++ and Kotlin must decode the same golden vectors.
- Physical Android, radio and power gates remain open until measured.
- M5 partition/OTA work and R1 dynamic applications remain separate.

## Non-goals

C1 does not add OTA, a partition redesign, a plugin store, a dynamic loader,
arbitrary sockets, IP over BLE, Bluetooth Classic, arbitrary cloud accounts,
a notification ecosystem, GPS/navigation, or a large Android UI.
