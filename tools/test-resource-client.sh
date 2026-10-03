#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/components/note4_companion/include" \
  -I"$root_dir/components/note4_connectivity/include" \
  "$root_dir/components/note4_companion/note4_companion_protocol.cc" \
  "$root_dir/components/note4_companion/note4_resource_gateway.cc" \
  "$root_dir/components/note4_companion/note4_connectivity_policy.cc" \
  "$root_dir/components/note4_connectivity/note4_wifi_backend.cc" \
  "$root_dir/components/note4_connectivity/note4_resource_client.cc" \
  "$root_dir/tools/resource_client_test.cc" -o "$test_binary"
"$test_binary"
printf 'PASS: resource client escalation and lifecycle tests.\n'
