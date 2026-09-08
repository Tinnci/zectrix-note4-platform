#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/tools/update_host_include" \
    -I"$repo_root/tools/host_include" \
    -I"$repo_root/components/zectrix_update/include" \
    "$repo_root/components/zectrix_update/zectrix_update_service.cc" \
    "$repo_root/components/zectrix_update/zectrix_update_stream.cc" \
    "$repo_root/components/zectrix_update/zectrix_update_esp.cc" \
    "$repo_root/tools/update_service_test.cc" \
    -o "$tmp_dir/update_service_test"

"$tmp_dir/update_service_test" "$@"
printf 'PASS: A/B boot protection, streamed CRC/header verification and OTA commit tests.\n'
