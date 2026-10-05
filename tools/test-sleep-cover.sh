#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
flags=(-Wall -Wextra -Werror -pedantic)
if [ "${NOTE4_SLEEP_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/components/note4_log/include" \
    -I"$root_dir/tools/host_include" \
    -I"$root_dir/components/note4_app/include" -I"$root_dir/components/note4_text/include" \
    -I"$root_dir/components/note4_reader/include" \
    -I"$root_dir/components/note4_power/include" \
    -I"$root_dir/components/note4_time/include" \
    "$root_dir/components/note4_app/note4_scene_manager.cc" \
    "$root_dir/components/note4_app/note4_sleep_cover.cc" \
    "$root_dir/tools/sleep_cover_test.cc" -o "$work_dir/test"
"$work_dir/test"
