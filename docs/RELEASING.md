# Note4 firmware releases

R2.1 supplies one local/GitHub build path for the Note4 ESP32-S3, three
configuration profiles and a bilingual illustrated handbook. User installation
is documented in [English](HANDBOOK.md) and [Chinese](HANDBOOK_zh.md).
The earlier G1.2 preview is recorded in [Release preparation](RELEASE_PREPARATION.md).

## Build the downloads

Use the qualified ESP-IDF v5.5.2 environment and a committed checkout:

```bash
source tools/activate-dev-env.sh
bash tools/test-host.sh
bash tools/build-release.sh
```

`version.txt` supplies ESP-IDF's embedded application version, currently
`1.2.0`. The download tag is `v1.2.0`; `--version v1.2.0-rc.1` permits a
prerelease of that base version. A prerelease suffix labels the download set;
the native descriptor continues to use the committed `version.txt` value.

The script builds `full`, `minimal` and `reader` from their committed Kconfig
profiles into separate directories, captures the existing build provenance,
exports the handbooks and packages the files into `build-release-v1.2.0/`.
An existing output directory is preserved; choose `--output ANOTHER_DIRECTORY`
for the next candidate. Saved developer `sdkconfig` values are not rewritten.
No command in this build path flashes a device or initializes its library.

For one matrix worker, use `--profile full` (or `minimal`/`reader`). Full's
worker also produces the common guides, host tools, example apps and optional
library initialization archive. The `reader` profile keeps Chinese/English
reading, USB maintenance/management and firmware writing, while excluding
Connectivity, Wi-Fi, Lua and Pocket Tools. All profiles retain the same
partition table and core boot protection.

To reuse builds that already have current provenance:

```bash
bash tools/build-release.sh --from-builds --output build-release-review
```

Rebuild and recapture provenance after source changes. Local dirty builds are
recorded as dirty for review; the publication workflow uses committed source.
Provenance identifies the environment and inputs, not a claim of independently
demonstrated byte-identical builds.

## Download inventory

| Asset | Purpose |
| --- | --- |
| `zectrix-note4-v1.2.0-{full,minimal,reader}.zip` | Complete segmented USB installation/recovery, native flash arguments, profile manifest and licenses |
| `zectrix-note4-v1.2.0-{full,minimal,reader}-app.bin` | Standalone native application image; not a complete USB installation |
| `zectrix-note4-v1.2.0-library-init.zip` | Explicit, optional replacement of the 4 MiB content partition with the bundled reading guide |
| `zectrix-note4-v1.2.0-host-tools.zip` | Portable USB manager, `.zapp` CLI and license; Python dependencies resolved by uv |
| `zectrix-note4-v1.2.0-handbook-{en,zh-CN}.html` | Offline, printable handbooks with embedded PNG illustrations |
| `Calculator.zapp`, `Flashcards.zapp` | Independently installable example apps for Full |
| `manifest.json` | Each profile's source/native version, IDF identity, enabled modules, partition map and size |
| `SHA256SUMS` | Integrity of every other download, including the combined manifest |

The standard set has fourteen files. There is no production-signed Android
APK in this set; Android source/build/qualification remains separately documented.

Firmware ZIPs contain only bootloader, partition table, factory application
and initial OTA selection. The packager retains separate segments to preserve
the gaps containing NVS and content. It rejects extra data segments, paths
outside the build directory, overlaps with protected data and oversized apps.
The separate library archive writes only `books` at `0x912000`; its instructions
explicitly identify the replacement of all books, apps and phone pictures.
Never include that image in an ordinary upgrade's flash arguments.

Handbook export uses Markdown through uv and embeds the existing firmware
screenshots. Language-switch links stay local; further repository references
point to the selected tag. The resulting HTML needs no JavaScript, external
font, image request or running server.

## GitHub matrix and publication

[`.github/workflows/release.yml`](../.github/workflows/release.yml) runs three
ESP-IDF build workers alongside the existing Host/static and Android CI jobs.
It reuses `CI` through `workflow_call` and skips that workflow's redundant
single firmware build. Each worker runs the same local release script.

The collector checks each worker's existing `SHA256SUMS` before copying files,
then checks common source/version/IDF/partition identity and writes a combined
manifest/checksum list. Corrupted, missing, duplicate or mixed worker outputs
leave no partially published local download directory. SHA-256 is confined
to the existing download boundary: Git, version labels, types and ordinary
tests cannot detect truncation of an already-built file in transit. No device
hash format, frozen source contract or additional required CI status is added.

After CI and all builds pass, the release job uploads a **draft**, removes stale
draft assets, downloads the complete set again and verifies the expected sums.
Only then can it publish. Publication of the same tag is serialized. An existing
tag must identify the build source, and an already-public release is never
overwritten. A failed upload/download/check leaves the draft unpublished and
can be retried. Existing branch protections and review requirements remain.

Once the workflow is on the remote default branch, prepare a draft with:

```bash
export SYNC_GH_IDENTITY=1
gh auth switch -u Tinnci
gh workflow run release.yml --ref main -f version=v1.2.0 -F publish=false
```

Review the draft's assets and the notes from
[`docs/releases/v1.2.0.md`](releases/v1.2.0.md). To publish the reviewed source,
push its version tag after that source has reached the remote repository:

```bash
git tag v1.2.0 SOURCE_COMMIT
git push origin v1.2.0
gh auth switch -u shisoratsu
```

Replace `SOURCE_COMMIT` with the reviewed source revision. A version-tag push
builds/checks that revision and publishes the completed download set. Manual
dispatch with `publish=true` is also supported. Tags containing a prerelease
suffix are marked prerelease. Update `version.txt` and the corresponding notes
for the next version; existing published assets remain immutable.

The workflow publishes USB firmware downloads. Trusted on-device OTA
discovery/authentication/install UX remains #55; download checksums are not
publisher signatures. Interrupted OTA qualification remains #56.

## Verification and milestone scope

The existing firmware-budget Host target exercises archive round trips, exact
flash bytes/addresses, preserved data ranges, source mismatches, failed
packaging, all three matrix profiles, library isolation and corrupted/incomplete
matrix downloads. The Kconfig suite tests the Reader profile's actual dependency
resolution. Run `shellcheck tools/*.sh` and `actionlint` for shell/workflow
validation. Firmware builds execute before native capacity reports.

The handbook is inspected at desktop and phone widths after HTML export.
The standard local package is unpacked and checked against the built images;
both example packages are inspected with the shipped host tools. Recorded
iteration results are in the [release notes](releases/v1.2.0.md).

R2.1 local validation passed all 41 Host targets, 40 Android JVM tests and the
debug build, three ESP32-S3 builds, the existing Full/Minimal comparison,
ShellCheck and Actionlint. Reader and Full both passed connected-device
flash/boot smoke with 11/15 catalog entries; the device finished on Full.
The original two-second USB Host-fixture deadline expired after 147 of 180
intentionally fragmented input bytes on this host. Its completion wait now
allows ten seconds, retaining every overflow/disconnect/stale-input assertion
without changing device timing or transport code.

R2.1 consolidates L1/S1/E1 and the later reliability, typography, Lua packaging
and companion integration work. The public milestone inventory on 2026-09-12
already has L1/S1/E1 closed and no separate R2 milestone. Research milestone 6
still records dynamic-runtime exploration; C1/D1 retain their original broader
physical qualification scope. Release preparation must not mark those physical
observations complete. NFC/BLE, USB/RF, sleep/RTC/current and display evidence
remain in #37/#38/#47/#48/#57 and their existing records.
