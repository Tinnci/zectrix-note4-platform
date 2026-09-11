#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
flags=()
if [ "${ZECTRIX_UTILITIES_SANITIZE:-0}" = 1 ]; then
    flags+=("-fsanitize=address,undefined" -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${flags[@]}" \
    -I"$repo_root/tools/host_include" \
    -I"$repo_root/components/zectrix_app/include" \
    -I"$repo_root/components/zectrix_time/include" \
    "$repo_root/components/zectrix_app/zectrix_utilities.cc" \
    "$repo_root/components/zectrix_app/zectrix_scene_manager.cc" \
    "$repo_root/tools/utilities_test.cc" -o "$test_binary"
"$test_binary"
