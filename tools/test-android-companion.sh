#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
case "$(uname -s)" in
  Darwin) android_default="$HOME/Library/Android/sdk" ;;
  Linux) android_default="$HOME/Android/Sdk" ;;
  *) echo 'FAIL: Android build supports macOS and Linux hosts.' >&2; exit 1 ;;
esac
export ANDROID_HOME=${ZECTRIX_ANDROID_SDK_ROOT:-${ANDROID_HOME:-$android_default}}
export ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$ANDROID_HOME}
gradle_wrapper="$root_dir/android-companion/gradlew"

if [ ! -x "$gradle_wrapper" ]; then
  echo 'FAIL: committed Gradle Wrapper is missing or not executable.' >&2
  exit 1
fi
java_version="$(java -version 2>&1 | awk -F '"' 'NR == 1 { print $2 }')"
case "$java_version" in
  21|21.*) ;;
  *) echo "FAIL: JDK 21 required, active version=${java_version:-unknown}." >&2; exit 1 ;;
esac
test -d "$ANDROID_HOME/platforms/android-37.0" || {
  echo "FAIL: Android platform 37.0 missing under $ANDROID_HOME." >&2; exit 1;
}
test -d "$ANDROID_HOME/build-tools/37.0.0" || {
  echo "FAIL: Android build-tools 37.0.0 missing under $ANDROID_HOME." >&2; exit 1;
}

gradle_tasks=(:app:testDebugUnitTest :app:assembleDebug)
if [ "${ZECTRIX_ANDROID_CLEAN:-0}" = 1 ]; then
  gradle_tasks=(clean "${gradle_tasks[@]}")
fi
"$gradle_wrapper" --no-daemon -p "$root_dir/android-companion" "${gradle_tasks[@]}"
echo 'PASS: Android companion JVM tests and debug build.'
