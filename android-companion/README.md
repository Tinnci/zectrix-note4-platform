# Zectrix Note4 Android companion

This is the Android side of C1. It is a real application, not an SDK sample.
It uses Android's built-in Kotlin support from Android Gradle Plugin 9.4.

## Toolchain

- minimum SDK: 26, because Companion Device Manager starts at Android 8;
- target and compile SDK: 37;
- Android Gradle Plugin: 9.4.0-alpha03, the version available for the pinned
  local Android 37 toolchain when C1 started;
- Gradle: 9.6.1;
- JDK: 21 on the development host.

The UI uses Jetpack Compose and the public Material 3 Expressive API from
Material 3 `1.5.0-alpha26`. The alpha dependency is isolated to the Android UI.
It does not define firmware or wire behavior.

The primary screen shows one connection state and one action. Technical state
remains in the connection-details card. The application distinguishes these
gates:

1. Companion Device Manager association;
2. Bluetooth bond and encrypted link;
3. notification transport;
4. protocol Hello/HelloAck;
5. protocol peer authorization.

All five gates are implemented in the current vertical slice. A successful
CCCD write alone is never reported as a secure, protocol-ready or authorized
connection.

The controlled resource gateway maps the protocol capability
`public_test_document_v1` to a fixed HTTPS endpoint owned by Android. Firmware
cannot provide a URL or credential. The gateway applies timeout, MIME, UTF-8
and 2048-byte limits, returns typed failures, deduplicates terminal request IDs
and permits bounded retry after offline or timeout results.

## Build and JVM test

```bash
source tools/activate-dev-env.sh
tools/test-android-companion.sh
```

The committed Gradle Wrapper is the only supported Gradle entry point. The
environment must expose JDK 21 and Android SDK platform 37 with build-tools
37.0.0. Set `ZECTRIX_ANDROID_CLEAN=1` for a clean rebuild instead of an
incremental developer build.

This builds a debug APK and runs the protocol, persistent queue and lifecycle
state-machine tests. C++ remains the normative producer of
`protocol/golden-vectors.json`; Kotlin must match its bytes.

## Physical gate

An APK build is not pairing evidence. C1.5 remains open until a real Android
device validates Companion Device Manager association, GATT lifecycle,
background presence, process restart, durable replay and unpair/re-pair.

The app does not use periodic background scanning as its primary reconnect
mechanism. It requests an association and registers a `CompanionDeviceService`.
The process-owned GATT client connects only to an approved association. It
serializes writes, supports fragmented frames and survives Activity teardown.
The `CompanionDeviceService` owns presence-based background connection. Failed
automatic attempts use bounded backoff; an explicit user action can retry at
once.

Fresh pairing still requires a local Note4 action. The user must open the
Note4 pairing window before Android can create a bond. A Companion Device
Manager association is not a Bluetooth bond.
