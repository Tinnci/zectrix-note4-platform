#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir/tools/ha-bridge"
bun test bridge.test.ts ../ui_preview_test.ts ../edge_page_server_test.ts
