#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_backend.cc" \
  "$root_dir/tools/wifi_backend_test.cc" -o "$test_binary"
"$test_binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -I"$root_dir/tools/wifi_driver_host_include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_backend.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_esp_driver.cc" \
  "$root_dir/tools/wifi_esp_driver_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: Wi-Fi bursts, radio shutdown, ESP event concurrency and DNS lifetime tests.'
