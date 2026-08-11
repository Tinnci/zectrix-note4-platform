#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$root_dir/components/zectrix_display/include" "$root_dir/components/zectrix_display/zectrix_display_state.cc" "$root_dir/tools/display_state_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: display state model tests.'
