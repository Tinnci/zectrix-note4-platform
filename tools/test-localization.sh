#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
reader_dir="$root_dir/components/note4_reader"
"${CC:-cc}" -DNOTE4_READER_FONT_PATH="\"$reader_dir/font/reader_font.bin\"" \
  -c "$reader_dir/note4_reader_font_data.S" -o "$work_dir/font.o"
flags=()
if [ "${NOTE4_LOCALIZATION_SANITIZE:-0}" = 1 ]; then
    flags+=("-fsanitize=address,undefined" -fno-omit-frame-pointer -g)
fi
for profile in full chinese-only english-only; do
    reader=0
    chinese=1
    sources=()
    if [ "$profile" = full ]; then
        reader=1
        sources+=("$reader_dir/note4_reader_font.cc" "$work_dir/font.o")
    elif [ "$profile" = english-only ]; then
        chinese=0
    fi
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${flags[@]}" \
      -DCONFIG_NOTE4_ENABLE_READER="$reader" -DCONFIG_NOTE4_ENABLE_UI_CHINESE="$chinese" \
      -DCONFIG_NOTE4_UI_DEFAULT_CHINESE=1 \
      -I"$root_dir/tools/host_include" \
      -I"$root_dir/components/note4_app/include" -I"$root_dir/components/note4_text/include" \
      -I"$root_dir/components/note4_time/include" \
      -I"$root_dir/components/note4_storage/include" \
      -I"$root_dir/components/ui/include" \
      -I"$root_dir/components/ui/font" -I"$reader_dir/include" \
      "$root_dir/components/note4_app/note4_locale.cc" \
      "$root_dir/components/note4_app/note4_language_setting.cc" \
      "$root_dir/components/note4_app/note4_scene_manager.cc" \
      "$root_dir/components/note4_app/note4_first_party_app_controllers.cc" \
      "$root_dir/components/ui/canvas.cc" \
      "$root_dir/components/ui/status_bar.cc" \
      "$root_dir/tools/localization_test.cc" "${sources[@]}" -o "$work_dir/$profile"
    "$work_dir/$profile"
done
