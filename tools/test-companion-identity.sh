#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$repo_root/components/zectrix_companion/include" \
  "$repo_root/components/zectrix_companion/zectrix_companion_identity.cc" \
  "$repo_root/tools/companion_identity_test.cc" \
  -o "$tmp_dir/companion_identity_test"

"$tmp_dir/companion_identity_test"
echo "PASS: companion identity record codec tests."
