# Continuous integration

The `CI` GitHub Actions workflow runs for pull requests to `main`, pushes to
`main` and manual dispatches. It grants read-only repository access and splits
validation into independent jobs so a failure identifies its platform:

- `Host tests and static checks` runs ShellCheck, architecture checks and the
  complete host test suite with two bounded workers. It also builds the interactive CLI simulator
  and runs terminal and pipe integration tests with Python through `uv`.
  Book-transfer tests use Bun and real local HTTP sockets with the production
  Storage and web API code.
- `Android companion` installs JDK 21 and the qualified Android SDK 37.0
  packages, then runs a clean JVM test and debug APK build with the committed
  Gradle Wrapper.
- `ESP32-S3 firmware` builds with the official ESP-IDF v5.5.2 container, runs
  the size report and captures build provenance. The container installs `uv`
  for the same Python artifact reporters used by local builds.

Host runs retain per-target logs and a JSON timing report, including on failure.
Successful runs retain the debug APK, firmware images, ELF, map, size report
and provenance for 14 days. Android test reports are uploaded for non-cancelled
runs, including failed tests when Gradle produced a report.

Third-party actions are limited to official GitHub, Gradle and Espressif
actions. Action references are pinned to immutable commits, with the audited
release version recorded in a comment. Dependabot proposes grouped weekly
updates instead of allowing action tags to change underneath an existing run.
Host integration tests install `uv` and Bun with their official standalone
installers. The HTTP tests use Bun 1.4.2, matching the HA bridge lockfile format.

Run the corresponding checks locally with:

```bash
shellcheck tools/*.sh
tools/test-host.sh
# List targets, run a group, or reproduce one failure sequentially.
tools/test-host.sh --list
tools/test-host.sh --suite ui --jobs 2
tools/test-host.sh --test reader --jobs 1 --verbose
ZECTRIX_ANDROID_CLEAN=1 tools/test-android-companion.sh
source tools/activate-dev-env.sh
tools/build-firmware.sh --clean
idf.py size
tools/capture-build-provenance.sh
```

## Host test groups and execution

The original 46 host script targets are retained. The scheduler self-test adds
one target, for 47 total. Architecture checks run once before the selected
targets. Individual `tools/test-*.sh` scripts remain independently runnable.

| Group | Targets | Coverage |
| --- | ---: | --- |
| `foundation` | 12 | Scheduler, app contract/runtime, controllers, scenes, SDK boundary, service registry, Lua runtime, module configurations, localization, input and layout |
| `connectivity` | 13 | Companion identity/protocol, settings/policy, NFC enrollment, pairing, radio arbitration, resource client/gateway, sync, Wi-Fi and edge page transfer |
| `ui` | 7 | Display service/state, digit quality/codecs, reader/font, sleep cover and utilities |
| `platform` | 8 | Firmware budget/packaging, health, platform profiles, power, storage, system, time and update |
| `integration` | 7 | Real HTTP book transfer, CLI core/diagnostics/terminal, HA bridge/preview, USB PTY and app packages |

Use repeated `--suite` or `--test` arguments to combine selections. Default
execution covers all groups with two workers; `--jobs 1` is the sequential
reference and `--jobs 4` is an explicit option for larger machines. Each target
has a 300-second timeout (`--timeout` overrides it). Failures do not hide the
remaining targets: logs are separated, failed logs are printed, and the final
exit status is nonzero. Ctrl+C or SIGTERM cancels queued work and terminates
the active process groups, including compilers and test servers.

Each invocation creates a unique directory under `build-host/host-tests/`
(`--report-dir` changes the parent). `results.json` records selection, worker
count, overall wall time and each target's duration/status/log path. The console
reports the five slowest targets. `--verbose` also prints successful logs.
Targets get isolated temporary directories; runtime builds use an isolated
directory and default to two compiler workers under the scheduler. Standalone
runtime builds retain their existing incremental build directory.

The HA bridge no longer reruns the edge page server suite: `edge-display` owns
that test, while HA still tests its authenticated page/telemetry integration.
Reader fixtures are generated once, then copied into independent directories
for connectivity-on and connectivity-off tests. Both configurations still run.

This is a mixed unit, simulation and integration suite, not 47 unit tests.
Isolation, fault injection, configuration variants and sanitizer switches are
existing strengths. Repeated compilation across script targets remains a cost;
use the timing report before migrating shared compilation into CMake/CTest or
adding a compiler cache. Such a migration must preserve target-specific defines,
fake drivers and sanitizer flags. Cold dependencies, CPU load and worker count
must be recorded when comparing runs. Host simulations do not qualify physical
radio, display or power behavior.

One local macOS comparison on 2026-10-03 passed all 47 script targets in both
modes: 321.51 seconds with one worker and 200.61 seconds with two workers
(37.6% less wall time). Both runs used isolated runtime builds and the same
default inner compiler limit. This is an observation, not a performance
threshold or a prediction for Linux CI. Platform profiles, display integration
and USB integration were the slowest targets.

The [release workflow](RELEASING.md) reuses CI through `workflow_call` for Host
and Android checks, alongside its Full/Minimal/Reader ESP32-S3 matrix. The
reused CI skips its duplicate firmware job. Version-tag pushes publish after
complete draft upload/download verification; manual runs default to drafts.
Packaging tests run with the existing firmware-budget Host target; no
additional required branch-status check is added. G1.2's earlier preview
remains documented in [Release preparation](RELEASE_PREPARATION.md).

CI does not replace real-device qualification. BLE pairing, NFC routing, USB
reconnect, Wi-Fi behavior, e-paper output, current draw, sleep and coexistence
remain physical evidence gates in their milestone issues.
