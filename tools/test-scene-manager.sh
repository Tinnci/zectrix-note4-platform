#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/components/zectrix_app/include" -I"$repo_root/components/zectrix_text/include" \
    "$repo_root/components/zectrix_app/zectrix_scene_manager.cc" \
    "$repo_root/components/zectrix_app/zectrix_gallery_controller.cc" \
    "$repo_root/tools/scene_manager_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: bounded scenes, deferred navigation and gallery lifecycle tests.'
