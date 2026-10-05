#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-Wall -Wextra -Werror -pthread)
if [ "${NOTE4_PLATFORM_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
common=(
  -I"$root_dir/components/note4_log/include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/note4_app/include" -I"$root_dir/components/note4_text/include" \
  -I"$root_dir/components/note4_board/include" \
  -I"$root_dir/components/note4_platform/include" \
  -I"$root_dir/components/note4_display/include" \
  -I"$root_dir/components/note4_epd/include" \
  -I"$root_dir/components/note4_epd/private_include" \
  -I"$root_dir/components/note4_input/include" \
  -I"$root_dir/components/note4_power/include" \
  -I"$root_dir/components/note4_storage/include" \
  -I"$root_dir/components/note4_self_test/include" \
  -I"$root_dir/components/note4_system/include" \
  -I"$root_dir/components/note4_time/include" \
  "$root_dir/components/note4_platform/note4_platform.cc" \
  "$root_dir/components/note4_platform/note4_service_registry.cc" \
  "$root_dir/components/note4_platform/note4_display_calibration_store.cc" \
  "$root_dir/components/note4_display/note4_display_model.cc" \
  "$root_dir/components/note4_display/note4_display_physics.cc" \
  "$root_dir/components/note4_epd/note4_epd_calibration.cc" \
  "$root_dir/components/note4_system/note4_boot_guard.cc" \
  "$root_dir/components/note4_system/note4_health_supervisor.cc" \
  "$root_dir/tools/platform_test.cc"
)
for profile in full minimal connectivity cli usb-host update; do
    connectivity=0 cli=0 update=0 reader=0 usb_host=0 runtime=0
    case "$profile" in
        full) connectivity=1 cli=1 update=1 reader=1 usb_host=1 runtime=1 ;;
        connectivity) connectivity=1 ;;
        cli) cli=1 ;;
        usb-host) cli=1 usb_host=1 ;;
        update) update=1 ;;
    esac
    optional=()
    if [ "$connectivity" = 1 ]; then
        optional+=(-I"$root_dir/components/note4_companion/include"
            -I"$root_dir/components/note4_connectivity/include"
            -I"$root_dir/components/note4_nfc_service/include")
    fi
    if [ "$cli" = 1 ]; then
        optional+=(-I"$root_dir/components/note4_cli/include"
            "$root_dir/components/note4_platform/note4_platform_diagnostics.cc"
            "$root_dir/components/note4_cli/note4_cli_core.cc"
            "$root_dir/components/note4_cli/note4_cli_control.cc"
            "$root_dir/components/note4_time/note4_time_sync.cc"
            "$root_dir/components/note4_cli/note4_cli_diagnostics.cc"
            "$root_dir/components/note4_log/note4_log.cc"
            "$root_dir/components/note4_cli/note4_cli_log.cc")
    fi
    if [ "$update" = 1 ]; then
        optional+=(-I"$root_dir/components/note4_update/include"
            "$root_dir/components/note4_update/note4_update_stream.cc")
    fi
    if [ "$usb_host" = 1 ]; then
        optional+=(-I"$root_dir/components/note4_host/include"
            "$root_dir/components/note4_host/note4_host_channel.cc"
            "$root_dir/components/note4_host/note4_host_protocol.cc")
    fi
    "${CXX:-c++}" -std=c++17 "${flags[@]}" \
        -DCONFIG_NOTE4_ENABLE_CONNECTIVITY="$connectivity" \
        -DCONFIG_NOTE4_ENABLE_READER="$reader" \
        -DCONFIG_NOTE4_ENABLE_RUNTIME="$runtime" \
        -DCONFIG_NOTE4_ENABLE_USB_CLI="$cli" -DCONFIG_NOTE4_ENABLE_UPDATE="$update" \
        -DCONFIG_NOTE4_ENABLE_USB_HOST="$usb_host" \
        "${common[@]}" "${optional[@]}" -o "$test_binary"
    "$test_binary"
    printf 'PASS: platform composition profile=%s.\n' "$profile"
done
