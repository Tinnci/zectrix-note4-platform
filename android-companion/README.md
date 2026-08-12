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

The project does not require Compose or a third-party UI framework. Its UI is
intentionally small: association state, BLE state, sync state and diagnostics.

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
The production GATT client will connect only to an approved association.
