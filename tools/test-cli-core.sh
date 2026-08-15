#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_root/components/zectrix_cli/include" \
  "$repo_root/components/zectrix_cli/zectrix_cli_core.cc" \
  "$repo_root/tools/cli_core_test.cc" \
  -o "$tmp_dir/cli_core_test"

"$tmp_dir/cli_core_test"
echo "PASS: bounded CLI core tests."
