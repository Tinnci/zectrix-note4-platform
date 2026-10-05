# ESP-IDF toolchain policy

## Reference baseline

The upstream reference project resolves these versions in `dependencies.lock`:

```text
source commit: ca285c98ed0641f86780edb1f5ec77b0335fe649
ESP-IDF: 5.5.2
esp_codec_dev: 1.5.11
target: esp32s3
```

This tuple is the permanent upstream reference baseline. It must remain
rebuildable for regression comparison even after the supported development
version advances.

## Supported development lane

ESP-IDF 5.5.2 remains the default device/release SDK. ESP-IDF 6.0.3 is a supported
build lane with separate Python environment, build directory and dependency
lock. Full builds are verified on both; device/RF/display/sleep qualification
for 6.0.3 remains required before making it the default release SDK.

An ESP-IDF version is not supported merely because it satisfies the manifest's
current `>=5.4.0` constraint. The constraint is a dependency compatibility
hint, not a qualification claim.

The project enables ESP-IDF's `MINIMAL_BUILD` property. Every used component
must declare its direct dependencies through `REQUIRES` or `PRIV_REQUIRES` so
clean builds compile only `main` and its transitive dependency graph.

## Selecting a build lane

Use `NOTE4_IDF_VERSION=6.0.3`, or an explicit `NOTE4_IDF_PATH`, when activating.
If an earlier activation exported `NOTE4_IDF_PATH`, unset it before selecting a
different version. `NOTE4_IDF_PYTHON_ENV_PATH` resolves multiple environments.
Tools select the matching SDK major/minor environment, never an inherited
environment for a different SDK. SDKs and existing user settings are not erased.

6.x uses `build-idf6[-profile]`, a build-owned component lock and `json2` size
reports. Direct GPIO/SPI and watchdog HAL dependencies are declared explicitly.
For 6.0's NimBLE one-bond NVS sort, CMake applies an adjacent-index capacity
bound to a build-owned source copy. It neither edits the SDK nor increases the
one-bond limit or suppresses compiler warnings.

## Reproducibility terminology

"Reproducible environment" means that source, IDF, component lock, target and
tool versions are recorded. "Bit-for-bit reproducible build" additionally
requires independent clean builds that produce identical artifacts. The latter
must be demonstrated before you make that stronger claim.

## Device/release promotion rule

```text
candidate version
  -> clean configure/build
  -> artifact size and hash comparison
  -> hardware self-tests
  -> EPD baseline regression
  -> power behavior check
  -> recorded decision
  -> default device/release SDK
```

Never update `dependencies.lock` as an incidental part of baseline bring-up.
A build-compatible lane can be maintained before this device qualification;
it must not be described as physically qualified or made the release default.
