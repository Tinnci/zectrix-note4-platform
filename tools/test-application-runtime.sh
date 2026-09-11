#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-Wall -Wextra -Werror)
if [ "${ZECTRIX_APP_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_app/include" -I"$root_dir/components/zectrix_text/include" \
  "$root_dir/components/zectrix_app/zectrix_app_contract.cc" \
  "$root_dir/components/zectrix_app/zectrix_application_runtime.cc" \
  "$root_dir/components/zectrix_app/zectrix_sdk_status.cc" \
  "$root_dir/tools/application_runtime_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: application runtime tests.'
