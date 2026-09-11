#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [ "${ZECTRIX_DISPLAY_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" -I"$root_dir/components/zectrix_display/include" \
    "$root_dir/components/zectrix_display/zectrix_display_state.cc" \
    "$root_dir/components/zectrix_display/zectrix_display_physics.cc" \
    "$root_dir/tools/display_state_test.cc" -o "$test_binary"
"$test_binary" "$@"
echo 'PASS: display lifecycle, physics simulation and telemetry recorder tests.'
