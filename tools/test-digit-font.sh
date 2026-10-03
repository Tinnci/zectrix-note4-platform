#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
uv run --no-project "$repo_dir/tools/digit_font_test.py" --fixtures "$work_dir/codec.bin"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [ "${ZECTRIX_DIGIT_SANITIZE:-0}" = 1 ]; then
    flags+=("-fsanitize=address,undefined" -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo_dir/components/zectrix_demo_ui/font" \
    "$repo_dir/tools/digit_codec_test.cc" -o "$work_dir/codec"
"$work_dir/codec" "$work_dir/codec.bin"
printf 'PASS: digit quality algorithms, lossless codecs and Python/C++ cross-decoding.\n'
