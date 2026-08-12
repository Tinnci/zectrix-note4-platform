#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/components/zectrix_app/include" \
    "$repo_root/components/zectrix_app/zectrix_first_party_app_controllers.cc" \
    "$repo_root/tools/first_party_app_controllers_test.cc" \
    -o "$test_binary"

"$test_binary"
echo "PASS: first-party application controller tests."
