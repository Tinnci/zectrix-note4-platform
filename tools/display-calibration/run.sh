#!/usr/bin/env bash
set -euo pipefail
tool_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror \
    "$tool_dir/catalog.cc" -o "$work_dir/catalog"
"$work_dir/catalog" "$@"
