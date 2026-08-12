#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir/components/zectrix_companion/include" \
  "$root_dir/components/zectrix_companion/zectrix_connectivity_policy.cc" \
  "$root_dir/tools/connectivity_policy_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: connectivity policy tests.'
