#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
flags=(-Wall -Wextra -Werror -pedantic -fno-rtti -fno-exceptions)
if [ "${ZECTRIX_SERVICE_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" \
    -I"$root_dir/components/zectrix_platform/include" \
    "$root_dir/components/zectrix_platform/zectrix_service_registry.cc" \
    "$root_dir/tools/service_registry_test.cc" -o "$work_dir/test"
"$work_dir/test"
