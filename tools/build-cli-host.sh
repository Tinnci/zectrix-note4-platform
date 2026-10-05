#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$#" -gt 1 ]]; then
    printf 'Usage: %s [output-binary]\n' "$0" >&2
    exit 2
fi
output="${1:-$repo_root/build-host/note4-cli-host}"
mkdir -p "$(dirname "$output")"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
    -I"$repo_root/components/note4_board/include" \
    -I"$repo_root/components/note4_time/include" \
    -I"$repo_root/components/note4_log/include" \
    -I"$repo_root/components/note4_cli/include" \
    -I"$repo_root/components/note4_system/include" \
    -I"$repo_root/components/note4_display/include" \
    -I"$repo_root/components/note4_epd/include" \
    -I"$repo_root/components/note4_epd/private_include" \
    "$repo_root/components/note4_display/note4_display_model.cc" \
  "$repo_root/components/note4_epd/note4_epd_calibration.cc" \
    "$repo_root/components/note4_cli/note4_cli_core.cc" \
    "$repo_root/components/note4_cli/note4_cli_session.cc" \
    "$repo_root/components/note4_cli/note4_cli_control.cc" \
    "$repo_root/components/note4_time/note4_time_sync.cc" \
    "$repo_root/components/note4_cli/note4_cli_diagnostics.cc" \
    "$repo_root/components/note4_log/note4_log.cc" \
    "$repo_root/components/note4_cli/note4_cli_log.cc" \
    "$repo_root/components/note4_display/note4_display_state.cc" \
    "$repo_root/components/note4_display/note4_display_physics.cc" \
    "$repo_root/tools/cli_host/stdio_transport.cc" \
    "$repo_root/tools/cli_host/simulated_platform.cc" \
    "$repo_root/tools/cli_host/main.cc" \
    -o "$output"

printf '%s\n' "$output"
