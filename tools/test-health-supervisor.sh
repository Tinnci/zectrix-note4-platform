#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-Wall -Wextra -Werror)
if [ "${ZECTRIX_HEALTH_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_system/include" \
  "$root_dir/components/zectrix_system/zectrix_health_supervisor.cc" \
  "$root_dir/tools/health_supervisor_test.cc" -o "$test_binary"
"$test_binary"
