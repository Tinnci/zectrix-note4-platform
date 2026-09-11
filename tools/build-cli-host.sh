#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$#" -gt 1 ]]; then
    printf 'Usage: %s [output-binary]\n' "$0" >&2
    exit 2
fi
output="${1:-$repo_root/build-host/zectrix-cli-host}"
mkdir -p "$(dirname "$output")"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
    -I"$repo_root/components/zectrix_board/include" \
    -I"$repo_root/components/zectrix_time/include" \
    -I"$repo_root/components/zectrix_cli/include" \
    -I"$repo_root/components/zectrix_system/include" \
    -I"$repo_root/components/zectrix_display/include" \
    "$repo_root/components/zectrix_cli/zectrix_cli_core.cc" \
    "$repo_root/components/zectrix_cli/zectrix_cli_session.cc" \
    "$repo_root/components/zectrix_cli/zectrix_cli_control.cc" \
    "$repo_root/components/zectrix_time/zectrix_time_sync.cc" \
    "$repo_root/components/zectrix_cli/zectrix_cli_diagnostics.cc" \
    "$repo_root/components/zectrix_cli/zectrix_cli_log.cc" \
    "$repo_root/components/zectrix_display/zectrix_display_state.cc" \
    "$repo_root/tools/cli_host/stdio_transport.cc" \
    "$repo_root/tools/cli_host/simulated_platform.cc" \
    "$repo_root/tools/cli_host/main.cc" \
    -o "$output"

printf '%s\n' "$output"
