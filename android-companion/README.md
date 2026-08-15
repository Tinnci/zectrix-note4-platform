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

Only gates 1-4 are implemented in the current vertical slice. A successful
CCCD write alone is never reported as a secure or protocol-ready connection.

## Build and JVM test

```bash
tools/test-android-companion.sh
```

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
