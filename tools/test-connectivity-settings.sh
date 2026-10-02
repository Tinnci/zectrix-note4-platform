#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_storage/include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_time/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_connectivity_settings.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_edge_settings.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_credentials.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_backend.cc" \
  "$root_dir/tools/connectivity_settings_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: connectivity settings and Wi-Fi credential persistence, failures and clearing.'
