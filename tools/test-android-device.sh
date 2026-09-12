#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
serial=${ZECTRIX_ANDROID_SERIAL:-$(adb get-serialno)}
work_dir=$(mktemp -d)
server_pid=""
port=""
cleanup() {
    if [ -n "$port" ]; then adb -s "$serial" reverse --remove "tcp:$port" >/dev/null 2>&1 || true; fi
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; fi
    rm -rf "$work_dir"
}
trap cleanup EXIT
"$root_dir/android-companion/gradlew" --no-daemon -p "$root_dir/android-companion" \
    :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest
mkdir -p "$work_dir/books"
"$root_dir/android-companion/build/companion-fixtures/book-web-host" "$work_dir/books" >"$work_dir/server.log" &
server_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
    if rg -q '^READY ' "$work_dir/server.log"; then break; fi
    kill -0 "$server_pid"
    sleep 0.05
done
port=$(sed -nE 's|^READY http://127.0.0.1:([0-9]+)/.*|\1|p' "$work_dir/server.log")
if [ -z "$port" ]; then cat "$work_dir/server.log" >&2; exit 1; fi
adb -s "$serial" reverse "tcp:$port" "tcp:$port"
adb -s "$serial" install -r "$root_dir/android-companion/app/build/outputs/apk/debug/app-debug.apk"
adb -s "$serial" install -r "$root_dir/android-companion/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"
if [ "$(adb -s "$serial" shell getprop ro.build.version.sdk | tr -d '\r')" -ge 37 ]; then
    adb -s "$serial" shell pm grant dev.zectrix.note4.companion android.permission.ACCESS_LOCAL_NETWORK
fi
adb -s "$serial" shell am instrument -w -r -e transferUrl "http://127.0.0.1:$port" \
    dev.zectrix.note4.companion.test/androidx.test.runner.AndroidJUnitRunner | tee "$work_dir/results.log"
rg -q '^OK \([0-9]+ tests?\)' "$work_dir/results.log"
echo 'PASS: Android intent, Keystore, durable storage, image conversion and native HTTP integration.'
echo 'NFC antenna, BLE pairing and background presence still require a physical Note4 and phone.'
