#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${1:-"$root_dir/build-companion/fixtures"}
mkdir -p "$work_dir"
companion="$root_dir/components/zectrix_companion"
connectivity="$root_dir/components/zectrix_connectivity"
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -pthread)
"${CXX:-c++}" "${flags[@]}" -I"$companion/include" \
    "$companion/zectrix_companion_protocol.cc" "$companion/zectrix_companion_identity.cc" \
    "$companion/zectrix_enrollment_ndef.cc" "$companion/zectrix_pairing_bootstrap.cc" \
    "$companion/zectrix_enrollment_publisher.cc" \
    "$companion/zectrix_sync_engine.cc" "$companion/zectrix_sync_session.cc" \
    "$root_dir/tools/companion_peer_host.cc" -o "$work_dir/companion-peer-host"
"${CC:-cc}" -DZECTRIX_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/zectrix_book_web_data.S" -o "$work_dir/web.o"
"${CXX:-c++}" "${flags[@]}" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" -I"$companion/include" \
    -I"$root_dir/components/zectrix_storage/include" \
    "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_transfer.cc" "$connectivity/zectrix_wifi_backend.cc" \
    "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
    "$root_dir/tools/book_web_host.cc" "$work_dir/web.o" -o "$work_dir/book-web-host"
echo 'Built production C++ companion and local HTTP fixtures.'
