#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# Match the Kconfig implementation used by the qualified ESP-IDF 5.5 toolchain.
uv run --no-project --with esp-idf-kconfig==2.5.4 "$root_dir/tools/module_config_test.py"
echo 'PASS: module defaults, dependencies and configuration changes.'
