#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_backend.cc" \
  "$root_dir/tools/wifi_backend_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: Wi-Fi backend state-machine tests.'
