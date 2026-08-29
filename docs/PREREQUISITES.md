# Development prerequisites

This document covers the host, repository, recovery and measurement gates. It
does not authorize flashing, erasing or eFuse operations.

## Host

The supported development hosts are Linux and macOS. Required commands are:

```text
gcc git make flex bison gperf python3 cmake ninja ccache
github-cli jq rg curl
```

Install ESP-IDF v5.5.2 with the official installer and the `esp32s3` target.
The qualified CMake version is 3.30.5. Install JDK 21 and Android SDK platform
37, build-tools 37.0.0 and platform-tools for the companion application.

Activate the environment per shell with the project helper. It selects
`esp32s3`, places the IDF Python environment before CMake, selects JDK 21 when
it can find one and uses the normal Android SDK location for the host:

```bash
source tools/activate-dev-env.sh
tools/check-dev-env.sh
```

Override non-standard locations before activation with
`ZECTRIX_IDF_PATH`, `ZECTRIX_IDF_PYTHON_ENV_PATH`,
`ZECTRIX_CMAKE_BIN_DIR`, `ZECTRIX_JAVA_HOME` and
`ZECTRIX_ANDROID_SDK_ROOT`. Do not put machine-specific absolute paths in the
repository.

Do not activate an ESP-IDF environment unconditionally from the shell startup
file. The admission test validates the controlled, materialized submodule set.
The minimal firmware build does not require unrelated MQTT, target or
OpenThread submodules to be initialized.

Build firmware through the project wrapper so a clean checkout cannot fall
back to ESP-IDF's default `esp32` target and ccache remains enabled:

```bash
tools/build-firmware.sh --clean
tools/capture-build-provenance.sh
```

The Android project uses its committed Gradle Wrapper. Do not require or use a
globally installed Gradle distribution:

```bash
tools/test-android-companion.sh
ZECTRIX_ANDROID_CLEAN=1 tools/test-android-companion.sh
```

## Network

The proxy must reach all of these endpoints:

```text
https://github.com
https://api.github.com
https://components.espressif.com
https://dl.google.com/android/repository/repository2-1.xml
https://services.gradle.org
```

GitHub API `EOF` failures are transport failures. Retry the request and record
the failure. Do not change repository state to compensate for a transient
proxy error.

## GitHub CLI

Use `gh milestone` for daily milestone operations, `gh issue` for issues, and
`gh label` for labels. Use `gh api` for idempotent bootstrap and special
fields. `gh-milestone` is optional command-syntax convenience only. It does
not repair proxy transport EOF failures. It is not a prerequisite gate.

## Device gate

Before development flashing:

1. Confirm the board model and serial device.
2. Run the private backup repository's `verify-backup.sh`.
3. Record the device identity and flash ID privately.
4. Confirm that no erase, write or eFuse command is part of the diagnostic.

The first development flash is a separate, explicitly approved action.

## EPD gate

Keep optical analysis in the EPD Lab repository's own environment. Do not
install scientific Python packages globally. Fix the camera, illumination,
exposure, gain, white balance, focus, temperature and battery conditions
before collecting baseline measurements.
