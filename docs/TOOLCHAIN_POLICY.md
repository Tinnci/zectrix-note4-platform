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

The supported lane is a separately qualified exact ESP-IDF version. Advance it
within the 5.5 maintenance line only after review of release notes, build
output, hardware self-tests, EPD behavior and power behavior.

An ESP-IDF version is not supported merely because it satisfies the manifest's
current `>=5.4.0` constraint. The constraint is a dependency compatibility
hint, not a qualification claim.

## Experimental lane

Major-version migrations, including ESP-IDF 6.x, are non-blocking experiments
until the supported lane passes all qualification gates.

## Reproducibility terminology

"Reproducible environment" means that source, IDF, component lock, target and
tool versions are recorded. "Bit-for-bit reproducible build" additionally
requires independent clean builds that produce identical artifacts. The latter
must be demonstrated before using that stronger claim.

## Promotion rule

```text
candidate version
  -> clean configure/build
  -> artifact size and hash comparison
  -> hardware self-tests
  -> EPD baseline regression
  -> power behavior check
  -> recorded decision
  -> supported lane
```

Never update `dependencies.lock` as an incidental part of baseline bring-up.
