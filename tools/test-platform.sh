#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
flags=(-Wall -Wextra -Werror -pthread)
if [ "${ZECTRIX_PLATFORM_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
common=(
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_app/include" \
  -I"$root_dir/components/zectrix_platform/include" \
  -I"$root_dir/components/zectrix_display/include" \
  -I"$root_dir/components/zectrix_input/include" \
  -I"$root_dir/components/zectrix_power/include" \
  -I"$root_dir/components/zectrix_storage/include" \
  -I"$root_dir/components/zectrix_self_test/include" \
  -I"$root_dir/components/zectrix_system/include" \
  -I"$root_dir/components/zectrix_time/include" \
  "$root_dir/components/zectrix_platform/zectrix_platform.cc" \
  "$root_dir/components/zectrix_platform/zectrix_service_registry.cc" \
  "$root_dir/components/zectrix_system/zectrix_boot_guard.cc" \
  "$root_dir/tools/platform_test.cc"
)
for profile in full core connectivity cli update; do
    connectivity=0 cli=0 update=0
    case "$profile" in
        full) connectivity=1 cli=1 update=1 ;;
        connectivity) connectivity=1 ;;
        cli) cli=1 ;;
        update) update=1 ;;
    esac
    optional=()
    if [ "$connectivity" = 1 ]; then
        optional+=(-I"$root_dir/components/zectrix_companion/include"
            -I"$root_dir/components/zectrix_connectivity/include"
            -I"$root_dir/components/zectrix_nfc_service/include")
    fi
    if [ "$cli" = 1 ]; then
        optional+=(-I"$root_dir/components/zectrix_cli/include"
            "$root_dir/components/zectrix_platform/zectrix_platform_diagnostics.cc"
            "$root_dir/components/zectrix_cli/zectrix_cli_core.cc"
            "$root_dir/components/zectrix_cli/zectrix_cli_control.cc"
            "$root_dir/components/zectrix_cli/zectrix_cli_diagnostics.cc"
            "$root_dir/components/zectrix_cli/zectrix_cli_log.cc")
    fi
    if [ "$update" = 1 ]; then
        optional+=(-I"$root_dir/components/zectrix_update/include"
            "$root_dir/components/zectrix_update/zectrix_update_stream.cc")
    fi
    "${CXX:-c++}" -std=c++17 "${flags[@]}" \
        -DCONFIG_ZECTRIX_ENABLE_CONNECTIVITY="$connectivity" \
        -DCONFIG_ZECTRIX_ENABLE_USB_CLI="$cli" -DCONFIG_ZECTRIX_ENABLE_UPDATE="$update" \
        "${common[@]}" "${optional[@]}" -o "$test_binary"
    "$test_binary"
    printf 'PASS: platform composition profile=%s.\n' "$profile"
done
