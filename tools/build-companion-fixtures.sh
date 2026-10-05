#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${1:-"$root_dir/build-companion/fixtures"}
mkdir -p "$work_dir"
companion="$root_dir/components/note4_companion"
connectivity="$root_dir/components/note4_connectivity"
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -pthread)
"${CXX:-c++}" "${flags[@]}" -I"$companion/include" \
    "$companion/note4_companion_protocol.cc" "$companion/note4_companion_identity.cc" \
    "$companion/note4_enrollment_ndef.cc" "$companion/note4_pairing_bootstrap.cc" \
    "$companion/note4_enrollment_publisher.cc" \
    "$companion/note4_sync_engine.cc" "$companion/note4_sync_session.cc" \
    "$root_dir/tools/companion_peer_host.cc" -o "$work_dir/companion-peer-host"
"${CC:-cc}" -DNOTE4_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/note4_book_web_data.S" -o "$work_dir/web.o"
"${CXX:-c++}" "${flags[@]}" \
    -I"$root_dir/components/note4_log/include" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" -I"$companion/include" \
    -I"$root_dir/components/note4_storage/include" \
    "$connectivity/note4_book_web.cc" "$connectivity/note4_book_transfer.cc" "$connectivity/note4_wifi_backend.cc" \
    "$root_dir/components/note4_storage/note4_book_storage.cc" \
    "$root_dir/tools/book_web_host.cc" "$work_dir/web.o" -o "$work_dir/book-web-host"
echo 'Built production C++ companion and local HTTP fixtures.'
