#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
connectivity="$root_dir/components/zectrix_connectivity"
flags=(-Wall -Wextra -Werror -pedantic -pthread)
if [ "${ZECTRIX_TRANSFER_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
"${CC:-cc}" -DZECTRIX_BOOK_WEB_PATH="\"$connectivity/web/books.html\"" \
    -c "$connectivity/zectrix_book_web_data.S" -o "$work_dir/web.o"
"$root_dir/tools/zapp" build "$root_dir/apps/Calculator.app.json" -o "$work_dir/Calculator.zapp"
for runtime in 1 0; do
    "${CXX:-c++}" -std=c++17 "${flags[@]}" \
        -DCONFIG_ZECTRIX_ENABLE_RUNTIME="$runtime" \
        -I"$root_dir/tools/host_include" -I"$connectivity/include" \
        -I"$root_dir/components/zectrix_storage/include" \
        -I"$root_dir/components/zectrix_companion/include" \
        -I"$root_dir/components/zectrix_app/include" -I"$root_dir/components/zectrix_text/include" \
        "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_transfer.cc" "$connectivity/zectrix_wifi_backend.cc" \
        "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
        "$root_dir/components/zectrix_app/zectrix_scene_manager.cc" \
        "$root_dir/components/zectrix_app/zectrix_book_transfer_controller.cc" \
        "$root_dir/tools/book_transfer_test.cc" "$work_dir/web.o" -o "$work_dir/test"
    "$work_dir/test" "$work_dir/runtime-$runtime" "$work_dir/Calculator.zapp"
done
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" \
    -I"$root_dir/components/zectrix_storage/include" -I"$root_dir/components/zectrix_companion/include" \
    "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_web_esp.cc" \
    "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
    "$root_dir/tools/book_web_esp_test.cc" "$work_dir/web.o" -o "$work_dir/esp-test"
"$work_dir/esp-test" "$work_dir"
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$root_dir/tools/host_include" -I"$connectivity/include" \
    -I"$root_dir/components/zectrix_storage/include" -I"$root_dir/components/zectrix_companion/include" \
    "$connectivity/zectrix_book_web.cc" "$connectivity/zectrix_book_transfer.cc" "$connectivity/zectrix_wifi_backend.cc" \
    "$root_dir/components/zectrix_storage/zectrix_book_storage.cc" \
    "$root_dir/tools/book_web_host.cc" "$work_dir/web.o" -o "$work_dir/book-web-host"
bun "$root_dir/tools/book_web_integration_test.mjs" "$work_dir/book-web-host" "$work_dir/network" "$work_dir/Calculator.zapp"
