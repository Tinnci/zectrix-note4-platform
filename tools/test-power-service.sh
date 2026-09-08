#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_power/include" \
  "$root_dir/components/zectrix_power/zectrix_power_service.cc" \
  "$root_dir/tools/power_service_test.cc" -o "$test_binary"
"$test_binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-const-variable -pthread \
  -I"$root_dir/tools/board_host_include" \
  -I"$root_dir/components/zectrix_board/include" \
  -I"$root_dir/components/zectrix_board" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_power/include" \
  -I"$root_dir/components/zectrix_nfc_service/include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_self_test" \
  "$root_dir/components/zectrix_board/zectrix_board.cc" \
  "$root_dir/components/zectrix_board/charge_status.cc" \
  "$root_dir/components/zectrix_board/i2c_bus_lock.cc" \
  "$root_dir/components/zectrix_board/i2c_device.cc" \
  "$root_dir/components/zectrix_board/rtc_pcf8563.cc" \
  "$root_dir/components/zectrix_board/zectrix_nfc.cc" \
  "$root_dir/components/zectrix_board/audio_codec.cc" \
  "$root_dir/components/zectrix_board/es8311_audio_codec.cc" \
  "$root_dir/components/zectrix_power/zectrix_power_service.cc" \
  "$root_dir/components/zectrix_nfc_service/zectrix_nfc_service.cc" \
  "$root_dir/components/zectrix_companion/zectrix_enrollment_ndef.cc" \
  "$root_dir/components/zectrix_self_test/acoustic_selftest.cc" \
  "$root_dir/tools/board_lifecycle_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: power service, board peripheral lifecycle and NFC teardown tests.'
