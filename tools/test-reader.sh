#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
reader_dir="$root_dir/components/zectrix_reader"
flags=(-Wall -Wextra -Werror)
if [ "${ZECTRIX_READER_SANITIZE:-0}" = 1 ]; then
    flags+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
uv run --no-project "$root_dir/tools/generate-reader-fixtures.py" "$work_dir"
"${CC:-cc}" -std=c99 "${flags[@]}" -I"$reader_dir/third_party/miniz" \
    -c "$reader_dir/third_party/miniz/miniz_tinfl.c" -o "$work_dir/inflate.o"
"${CC:-cc}" -DZECTRIX_READER_FONT_PATH="\"$reader_dir/font/reader_font.bin\"" \
    -c "$reader_dir/zectrix_reader_font_data.S" -o "$work_dir/font.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" \
    -I"$reader_dir/include" -I"$reader_dir/private" -I"$reader_dir/third_party/miniz" \
    "$reader_dir/zectrix_reader.cc" "$reader_dir/zectrix_reader_zip.cc" \
    "$reader_dir/zectrix_reader_text.cc" "$reader_dir/zectrix_reader_font.cc" \
    "$reader_dir/zectrix_reader_bookmarks.cc" \
    "$root_dir/tools/reader_test.cc" "$work_dir/inflate.o" "$work_dir/font.o" \
    -o "$work_dir/reader_test"
"$work_dir/reader_test" "$work_dir"
echo 'PASS: streaming TXT/EPUB, Unicode layout, navigation and durable bookmarks.'
