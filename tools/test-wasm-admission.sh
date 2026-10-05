#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
flags=(-std=c11 -O1 -Wall -Wextra -Werror)
if [[ "${NOTE4_WASM_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
"${CC:-cc}" "${flags[@]}" -I"$repo_dir/components/note4_runtime/include" \
    "$repo_dir/components/note4_runtime/note4_wasm_admission.c" \
    "$repo_dir/tools/wasm_admission_test.c" -o "$work_dir/test"
"$work_dir/test"
printf 'PASS: bounded Wasm admission, initialization and memory policy.\n'
