#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ANDROID_HOME=${ANDROID_HOME:-/opt/android-sdk}
export ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$ANDROID_HOME}
gradle --no-daemon -p "$root_dir/android-companion" \
  :app:testDebugUnitTest :app:assembleDebug
echo 'PASS: Android companion JVM tests and debug build.'
