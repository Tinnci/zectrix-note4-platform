#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"$repo_root/components/zectrix_cli/include" \
  -I"$repo_root/components/zectrix_system/include" \
  -I"$repo_root/components/zectrix_display/include" \
  "$repo_root/components/zectrix_cli/zectrix_cli_core.cc" \
  "$repo_root/components/zectrix_cli/zectrix_cli_session.cc" \
  "$repo_root/components/zectrix_cli/zectrix_cli_control.cc" \
  "$repo_root/components/zectrix_cli/zectrix_cli_diagnostics.cc" \
  "$repo_root/components/zectrix_cli/zectrix_cli_log.cc" \
  "$repo_root/tools/cli_diagnostics_test.cc" \
  -o "$tmp_dir/cli_diagnostics_test"

"$tmp_dir/cli_diagnostics_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"$repo_root/tools/host_include" \
  -I"$repo_root/components/zectrix_cli/include" \
  "$repo_root/components/zectrix_cli/zectrix_cli_log.cc" \
  "$repo_root/components/zectrix_cli/zectrix_cli_log_esp.cc" \
  "$repo_root/tools/cli_log_esp_test.cc" \
  -o "$tmp_dir/cli_log_esp_test"

"$tmp_dir/cli_log_esp_test"
echo 'PASS: CLI diagnostics, owner dispatch, cancellation and log tests.'
