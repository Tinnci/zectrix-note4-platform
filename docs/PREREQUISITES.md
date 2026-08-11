# Development prerequisites

This document covers the host, repository, recovery and measurement gates. It
does not authorize flashing, erasing or eFuse operations.

## Host

The reference host is Arch-family Linux. Required commands are:

```text
gcc git make flex bison gperf python3 pip3 cmake ninja ccache
dfu-util libusb github-cli jq rg curl
```

Install ESP-IDF v5.5.2 with the official installer and the `esp32s3` target.
Activate it per shell with the project helper. The helper places the IDF
Python environment before the separate CMake 3.30.5 environment:

```bash
source tools/activate-dev-env.sh
bash tools/check-dev-env.sh
```

Do not activate an ESP-IDF environment unconditionally from the shell startup
file. The admission test validates the controlled, materialized submodule set.
It does not require every ESP-IDF submodule to be initialized.

## Network

The proxy must reach all of these endpoints:

```text
https://github.com
https://api.github.com
https://components.espressif.com
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
