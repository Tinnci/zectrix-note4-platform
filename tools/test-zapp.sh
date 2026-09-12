#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
flags=(-std=c++17 -O1 -Wall -Wextra -Werror -pthread)
if [ "${ZECTRIX_ZAPP_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/tools/host_include" -I"$repo/components/zectrix_storage/include" \
    "$repo/components/zectrix_storage/zectrix_book_storage.cc" "$repo/tools/zapp_test.cc" -o "$work/test"
uv run --no-project "$repo/tools/zapp_test.py" "$work/test"
