# Continuous integration

The `CI` GitHub Actions workflow runs for pull requests to `main`, pushes to
`main` and manual dispatches. It grants read-only repository access and splits
validation into independent jobs so a failure identifies its platform:

- `Host tests and static checks` runs ShellCheck, architecture checks and the
  complete C++ host test suite.
- `Android companion` installs JDK 21 and the qualified Android SDK 37.0
  packages, then runs a clean JVM test and debug APK build with the committed
  Gradle Wrapper.
- `ESP32-S3 firmware` builds with the official ESP-IDF v5.5.2 container, runs
  the size report and captures build provenance.

Successful runs retain the debug APK, firmware images, ELF, map, size report
and provenance for 14 days. Android test reports are uploaded for non-cancelled
runs, including failed tests when Gradle produced a report.

Third-party execution is limited to official GitHub, Gradle and Espressif
actions. Action references are pinned to immutable commits, with the audited
release version recorded in a comment. Dependabot proposes grouped weekly
updates instead of allowing action tags to change underneath an existing run.

Run the corresponding checks locally with:

```bash
shellcheck tools/*.sh
tools/test-host.sh
ZECTRIX_ANDROID_CLEAN=1 tools/test-android-companion.sh
source tools/activate-dev-env.sh
tools/build-firmware.sh --clean
idf.py size
tools/capture-build-provenance.sh
```

CI does not replace real-device qualification. BLE pairing, NFC routing, USB
reconnect, Wi-Fi behavior, e-paper output, current draw, sleep and coexistence
remain physical evidence gates in their milestone issues.
