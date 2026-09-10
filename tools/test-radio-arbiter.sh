#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
connectivity="$root_dir/components/zectrix_connectivity"
companion="$root_dir/components/zectrix_companion"
flags=(-Wall -Wextra -Werror -pedantic -pthread)
if [ "${ZECTRIX_RADIO_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CC:-cc}" -DZECTRIX_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/zectrix_book_web_data.S" -o "$work_dir/web.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" \
    -I"$companion/include" -I"$root_dir/components/zectrix_storage/include" \
    "$connectivity/zectrix_radio_arbiter.cc" \
    "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_transfer.cc" \
    "$connectivity/zectrix_wifi_backend.cc" "$connectivity/zectrix_resource_client.cc" \
    "$companion/zectrix_companion_protocol.cc" "$companion/zectrix_sync_engine.cc" \
    "$companion/zectrix_sync_session.cc" "$companion/zectrix_connectivity_policy.cc" \
    "$companion/zectrix_resource_gateway.cc" \
    "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
    "$root_dir/tools/radio_arbiter_test.cc" "$work_dir/web.o" -o "$work_dir/test"
"$work_dir/test" "$work_dir"
