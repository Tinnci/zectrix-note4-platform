#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
"${CXX:-c++}" "${flags[@]}" \
    -I"$repo_dir/components/ui/include" \
    "$repo_dir/tools/layout_test.cc" -o "$work_dir/layout_test"
"$work_dir/layout_test"
