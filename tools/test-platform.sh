#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_platform/include" \
  -I"$root_dir/components/zectrix_display/include" \
  -I"$root_dir/components/zectrix_input/include" \
  -I"$root_dir/components/zectrix_power/include" \
  -I"$root_dir/components/zectrix_storage/include" \
  -I"$root_dir/components/zectrix_self_test/include" \
  -I"$root_dir/components/zectrix_system/include" \
  -I"$root_dir/components/zectrix_time/include" \
  "$root_dir/components/zectrix_platform/zectrix_platform.cc" \
  "$root_dir/tools/platform_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: platform composition tests.'
