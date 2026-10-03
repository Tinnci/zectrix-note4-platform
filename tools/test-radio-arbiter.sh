#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
connectivity="$root_dir/components/note4_connectivity"
companion="$root_dir/components/note4_companion"
flags=(-Wall -Wextra -Werror -pedantic -pthread)
if [ "${NOTE4_RADIO_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CC:-cc}" -DNOTE4_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/note4_book_web_data.S" -o "$work_dir/web.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" \
    -I"$companion/include" -I"$root_dir/components/note4_storage/include" \
    "$connectivity/note4_radio_arbiter.cc" \
    "$connectivity/note4_book_web.cc" "$connectivity/note4_book_transfer.cc" \
    "$connectivity/note4_wifi_backend.cc" "$connectivity/note4_resource_client.cc" \
    "$companion/note4_companion_protocol.cc" "$companion/note4_sync_engine.cc" \
    "$companion/note4_sync_session.cc" "$companion/note4_connectivity_policy.cc" \
    "$companion/note4_resource_gateway.cc" \
    "$root_dir/components/note4_storage/note4_book_storage.cc" \
    "$root_dir/tools/radio_arbiter_test.cc" "$work_dir/web.o" -o "$work_dir/test"
"$work_dir/test" "$work_dir"
