#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
reader_dir="$repo_dir/components/note4_reader"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
uv run --no-project "$repo_dir/tools/reader_font_test.py" "$work_dir"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [ "${NOTE4_READER_SANITIZE:-0}" = 1 ]; then
    flags+=(-g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
for source in fixture product; do
    reference=()
    font="$work_dir/fixture.bin"
    if [ "$source" = fixture ]; then
        reference=("$work_dir/expected.raw")
    else
        font="$reader_dir/font/reader_font.bin"
        if [ -n "${NOTE4_FONT_REFERENCE:-}" ]; then reference=("$NOTE4_FONT_REFERENCE"); fi
    fi
    "${CC:-cc}" -DNOTE4_READER_FONT_PATH="\"$font\"" \
        -c "$reader_dir/note4_reader_font_data.S" -o "$work_dir/font.o"
    "${CXX:-c++}" "${flags[@]}" -I"$reader_dir/include" -I"$repo_dir/components/note4_text/include" \
        "$reader_dir/note4_reader_font.cc" "$repo_dir/tools/reader_font_test.cc" \
        "$work_dir/font.o" -o "$work_dir/font-test"
    "$work_dir/font-test" "${reference[@]}"
done
