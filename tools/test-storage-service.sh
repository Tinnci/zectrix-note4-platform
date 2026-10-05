#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-Wall -Wextra -Werror)
if [ "${NOTE4_STORAGE_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
  -I"$root_dir/components/note4_log/include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/note4_storage/include" \
  -I"$root_dir/components/note4_platform/include" \
  -I"$root_dir/components/note4_display/include" \
  -I"$root_dir/components/note4_epd/include" \
  -I"$root_dir/components/note4_epd/private_include" \
  "$root_dir/components/note4_display/note4_display_model.cc" \
  "$root_dir/components/note4_display/note4_display_physics.cc" \
  "$root_dir/components/note4_epd/note4_epd_calibration.cc" \
  "$root_dir/components/note4_platform/note4_display_calibration_store.cc" \
  "$root_dir/components/note4_storage/note4_storage_service.cc" \
  "$root_dir/components/note4_storage/note4_book_storage.cc" \
  "$root_dir/tools/storage_service_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: storage service tests.'
