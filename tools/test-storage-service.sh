#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_storage/include" \
  "$root_dir/components/zectrix_storage/zectrix_storage_service.cc" \
  "$root_dir/tools/storage_service_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: storage service tests.'
