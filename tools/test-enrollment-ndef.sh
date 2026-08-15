#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$repo_root/components/zectrix_companion/include" \
  "$repo_root/components/zectrix_companion/zectrix_enrollment_ndef.cc" \
  "$repo_root/tools/enrollment_ndef_test.cc" \
  -o "$tmp_dir/enrollment_ndef_test"

"$tmp_dir/enrollment_ndef_test"
echo "PASS: enrollment NDEF v1 codec tests."
