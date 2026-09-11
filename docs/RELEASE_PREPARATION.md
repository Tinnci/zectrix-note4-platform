# v1.2 firmware preparation

G1.2 prepares the `v1.2.0-preview.1` draft for the Note4 ESP32-S3. It contains
Full and Minimal firmware from one source revision. The package label identifies
this preview; `manifest.json` also records the firmware's existing Git-derived
version, source commit, dirty state, ESP-IDF revision and native partition map.
Publishing the production release and user handbook remains R2.1.

## Build and package

Use a committed checkout and the qualified ESP-IDF v5.5.2 environment:

```bash
source tools/activate-dev-env.sh
bash tools/test-host.sh
for profile in full minimal; do
    bash tools/build-firmware.sh --profile "$profile"
    bash tools/capture-build-provenance.sh "build-$profile"
done
uv run --no-project tools/compare-firmware-profiles.py build-full build-minimal \
    --output build-full/profile-comparison.json
uv run --no-project tools/firmware_package.py --version v1.2.0-preview.1 \
    --output build-release-v1.2
```

The existing build and provenance commands supply the inputs; the packager
records that provenance and copies the images without rebuilding them. Rebuild and recapture
provenance after a source change. Full/Minimal source versions, IDF revisions
and partition maps must agree. An existing output directory is preserved;
use another output path for another candidate.

The resulting downloads are:

| File | Purpose |
| --- | --- |
| `zectrix-note4-v1.2.0-preview.1-full.zip` | Full initial/recovery image set and portable flashing arguments |
| `zectrix-note4-v1.2.0-preview.1-minimal.zip` | Minimal initial/recovery image set with the same partition layout |
| `zectrix-note4-v1.2.0-preview.1-{full,minimal}-app.bin` | Standalone application images for the existing image tooling |
| `manifest.json` | Source/build identity, sizes and native partition maps for both profiles |
| `SHA256SUMS` | Download-integrity checks for these files |

Each ZIP includes the separate bootloader, partition table, application and
initial OTA selection, plus `flash_args`, `flasher_args.json`, a profile manifest,
instructions and license notices. It copies the built image bytes unchanged.
It rejects missing/out-of-tree inputs, additional data images, segments that
overlap NVS/books/PHY data, mixed revisions and application images larger than
the installed slots. A failed second profile leaves no partial download set.

SHA-256 serves the download boundary: a file can be truncated or corrupted
after a successful build. Git, version labels and tests identify the source
and expected behavior but do not verify the downloaded bytes. No new runtime
hash, frozen source baseline or CI approval condition is introduced. The sums
are integrity evidence, not publisher authentication for the future #55 workflow.

Verify a downloaded set with `sha256sum -c SHA256SUMS` on Linux or
`shasum -a 256 -c SHA256SUMS` on macOS.

## Installation scope

Extract a ZIP, enter that directory and follow its README with ESP-IDF's
`esptool.py --chip esp32s3 --port PORT write_flash @flash_args`. This writes the
factory image, bootloader, table and OTA selection. It preserves NVS and books
on the existing Note4 partition layout; back up content before installation.
The packager deliberately retains separate flash segments because a merged
image padded across their gaps could erase those records. It performs no
flashing, whole-chip erase or partition migration.

The standalone application binary is not a complete initial USB installation.
The repository's bounded UpdateService remains available, while a trusted
firmware-download/user-update flow is still tracked in #55. The package does
not initialize the book filesystem or install sample books/scripts.

## Qualification and draft release

G1.2 reruns the 40-target Host suite, including four package tests that unpack
the archives, compare image bytes and flash addresses, verify download sums,
and exercise failed/mixed packaging. Full/Minimal builds and their existing
profile comparison accompany the draft. GitHub CI covers Linux Host tests,
the Android JVM/debug build and the ESP32-S3 firmware build.

D1.5's recorded Full device boot and 95.15-second runtime-watchdog observation
remain useful preceding hardware evidence; the G1.2 packaging changes do not
constitute another physical firmware qualification. Real interrupted OTA/A-B
rollback remains #56. Display/reader/transfer/sleep/RTC measurements remain #57,
with RF/Android evidence in #37/#38. Seven simulated health days do not represent
seven days on physical hardware.

The GitHub draft uses `v1.2.0-preview.1` and the source commit in the manifests.
Drafting and attaching the downloads does not publish a production release.

The G1.2 candidate was built from clean `c4014868a6d18525a8606886b132dd6fb9da9ea2`.
Its native version is `v1.0.0-129-gc401486`. Full/Minimal application sizes are
2,495,312 / 521,648 bytes; static internal RAM is 201,815 / 108,323 bytes. Both
archives passed extraction and byte/address checks. The candidate's
[CI run](https://github.com/Tinnci/zectrix-note4-platform/actions/runs/34652131070)
passed Host/static checks, Android and ESP32-S3 builds. The assets are attached
to the [draft release](https://github.com/Tinnci/zectrix-note4-platform/releases/tag/untagged-f5a3e8f7c85ed84e2c08).
The uploaded set was downloaded again and verified against SHA256SUMS.
