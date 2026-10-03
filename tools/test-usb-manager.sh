#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
flags=(-std=c++17 -O1 -Wall -Wextra -Werror -pthread)
if [ "${NOTE4_USB_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
includes=(
    -I"$root_dir/tools/host_include"
    -I"$root_dir/components/note4_host/include"
    -I"$root_dir/components/note4_cli/include"
    -I"$root_dir/components/note4_board/include"
    -I"$root_dir/components/note4_storage/include"
    -I"$root_dir/components/note4_system/include"
    -I"$root_dir/components/note4_display/include"
    -I"$root_dir/components/note4_app/include" -I"$root_dir/components/note4_text/include"
    -I"$root_dir/components/note4_time/include"
    -I"$root_dir/components/note4_power/include"
)
sources=(
    "$root_dir/components/note4_host/note4_host_channel.cc"
    "$root_dir/components/note4_host/note4_host_protocol.cc"
    "$root_dir/components/note4_host/note4_host_books.cc"
    "$root_dir/components/note4_storage/note4_book_storage.cc"
    "$root_dir/components/note4_cli/note4_cli_core.cc"
    "$root_dir/components/note4_cli/note4_cli_session.cc"
    "$root_dir/components/note4_cli/note4_cli_control.cc"
    "$root_dir/components/note4_cli/note4_cli_diagnostics.cc"
    "$root_dir/components/note4_time/note4_time_sync.cc"
    "$root_dir/components/note4_cli/note4_cli_log.cc"
)
for runtime in 1 0; do
    "${CXX:-c++}" "${flags[@]}" "${includes[@]}" "${sources[@]}" \
        -DCONFIG_NOTE4_ENABLE_RUNTIME="$runtime" \
        "$root_dir/components/note4_app/note4_usb_manager.cc" \
        "$root_dir/components/note4_app/note4_locale.cc" \
        "$root_dir/components/note4_app/note4_language_setting.cc" \
        "$root_dir/tools/usb_manager_test.cc" -o "$work_dir/test"
    "$work_dir/test" "$work_dir/runtime-$runtime"
done
"${CXX:-c++}" "${flags[@]}" "${includes[@]}" "${sources[@]}" \
    "$root_dir/tools/cli_host/stdio_transport.cc" \
    "$root_dir/tools/usb_manager_host.cc" -o "$work_dir/host"
uv run --no-project --with pyserial==3.5 "$root_dir/tools/usb_manager_integration_test.py" "$work_dir/host" "$work_dir/integration"
printf 'PASS: USB framing, lifecycle, books, apps, settings, controls and host tool integration.\n'
