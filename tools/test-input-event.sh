#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_input/include" \
  "$root_dir/components/zectrix_input/zectrix_input_service.cc" \
  "$root_dir/tools/input_event_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: input event tests.'
