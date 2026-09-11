#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
uv run --no-project "$repo_dir/tools/firmware_budget_test.py"
printf 'PASS: firmware budget and partition-aware boot reporting.\n'
