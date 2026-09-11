#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
reader_dir="$root_dir/components/zectrix_reader"
"${CC:-cc}" -DZECTRIX_READER_FONT_PATH="\"$reader_dir/font/reader_font.bin\"" \
  -c "$reader_dir/zectrix_reader_font_data.S" -o "$work_dir/font.o"
flags=()
if [ "${ZECTRIX_LOCALIZATION_SANITIZE:-0}" = 1 ]; then
    flags+=("-fsanitize=address,undefined" -fno-omit-frame-pointer -g)
fi
for profile in full chinese-only english-only; do
    reader=0
    chinese=1
    sources=()
    if [ "$profile" = full ]; then
        reader=1
        sources+=("$reader_dir/zectrix_reader_font.cc" "$work_dir/font.o")
    elif [ "$profile" = english-only ]; then
        chinese=0
    fi
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${flags[@]}" \
      -DCONFIG_ZECTRIX_ENABLE_READER="$reader" -DCONFIG_ZECTRIX_ENABLE_UI_CHINESE="$chinese" \
      -DCONFIG_ZECTRIX_UI_DEFAULT_CHINESE=1 \
      -I"$root_dir/tools/host_include" \
      -I"$root_dir/components/zectrix_app/include" -I"$root_dir/components/zectrix_text/include" \
      -I"$root_dir/components/zectrix_time/include" \
      -I"$root_dir/components/zectrix_storage/include" \
      -I"$root_dir/components/zectrix_demo_ui/include" \
      -I"$root_dir/components/zectrix_demo_ui/font" -I"$reader_dir/include" \
      "$root_dir/components/zectrix_app/zectrix_locale.cc" \
      "$root_dir/components/zectrix_app/zectrix_language_setting.cc" \
      "$root_dir/components/zectrix_app/zectrix_scene_manager.cc" \
      "$root_dir/components/zectrix_app/zectrix_first_party_app_controllers.cc" \
      "$root_dir/components/zectrix_demo_ui/zectrix_canvas.cc" \
      "$root_dir/components/zectrix_demo_ui/zectrix_status_bar.cc" \
      "$root_dir/tools/localization_test.cc" "${sources[@]}" -o "$work_dir/$profile"
    "$work_dir/$profile"
done
