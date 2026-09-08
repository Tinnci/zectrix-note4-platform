#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

binary="$(bash "$repo_root/tools/build-cli-host.sh" "$tmp_dir/zectrix-cli-host")"
uv run --no-project --offline python "$repo_root/tools/cli_host_integration_test.py" "$binary"
printf 'PASS: host CLI terminal, pipe, reconnect and shutdown tests.\n'
