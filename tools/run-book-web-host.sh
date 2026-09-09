#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage: %s BOOK_DIRECTORY [PORT]\n' "$0" >&2
    exit 2
fi
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
connectivity="$root_dir/components/zectrix_connectivity"
"${CC:-cc}" -DZECTRIX_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/zectrix_book_web_data.S" -o "$work_dir/web.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" \
    -I"$root_dir/components/zectrix_storage/include" -I"$root_dir/components/zectrix_companion/include" \
    "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_transfer.cc" "$connectivity/zectrix_wifi_backend.cc" \
    "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
    "$root_dir/tools/book_web_host.cc" "$work_dir/web.o" -o "$work_dir/book-web-host"
"$work_dir/book-web-host" "$@"
