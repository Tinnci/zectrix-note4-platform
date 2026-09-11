#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
flags=(-Wall -Wextra -Werror -pedantic)
if [ "${ZECTRIX_SLEEP_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" \
    -I"$root_dir/components/zectrix_app/include" -I"$root_dir/components/zectrix_text/include" \
    -I"$root_dir/components/zectrix_reader/include" \
    -I"$root_dir/components/zectrix_power/include" \
    -I"$root_dir/components/zectrix_time/include" \
    "$root_dir/components/zectrix_app/zectrix_scene_manager.cc" \
    "$root_dir/components/zectrix_app/zectrix_sleep_cover.cc" \
    "$root_dir/tools/sleep_cover_test.cc" -o "$work_dir/test"
"$work_dir/test"
