#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$root_dir/components/zectrix_storage/include" \
  "$root_dir/components/zectrix_connectivity/zectrix_edge_sync.cc" \
  "$root_dir/components/zectrix_connectivity/zectrix_wifi_backend.cc" \
  "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
  "$root_dir/tools/edge_sync_test.cc" -o "$work_dir/test"
"$work_dir/test" "$work_dir/cache"
bun test "$root_dir/tools/edge_page_server_test.ts"
echo 'PASS: edge page transfer budget, cancellation, timer wrap and atomic cache replacement.'
