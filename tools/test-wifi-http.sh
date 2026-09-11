#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_time/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_http.cc" \
  "$root_dir/components/zectrix_time/zectrix_time_sync.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_http_response.cc" \
  "$root_dir/tools/wifi_http_test.cc" -o "$test_binary"
"$test_binary"
printf 'PASS: bounded HTTP response and non-blocking transport tests.\n'
