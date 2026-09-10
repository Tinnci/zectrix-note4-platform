#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
test_binary="$work_dir/display_service_test"
trap 'rm -rf "$work_dir"' EXIT
reader_dir="$root_dir/components/zectrix_reader"
"${CC:-cc}" -std=c99 -I"$reader_dir/third_party/miniz" \
  -c "$reader_dir/third_party/miniz/miniz_tinfl.c" -o "$work_dir/inflate.o"
"${CC:-cc}" -DZECTRIX_READER_FONT_PATH="\"$reader_dir/font/reader_font.bin\"" \
  -c "$reader_dir/zectrix_reader_font_data.S" -o "$work_dir/font.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/epd_host_include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_display/include" \
  -I"$root_dir/components/zectrix_epd/include" \
  -I"$root_dir/components/zectrix_epd/private_include" \
  -I"$root_dir/components/zectrix_demo_ui/include" \
  -I"$root_dir/components/zectrix_demo_ui/font" \
  -I"$root_dir/components/zectrix_app/include" \
  -I"$root_dir/components/zectrix_host/include" \
  -I"$root_dir/components/zectrix_storage/include" \
  -I"$root_dir/components/zectrix_board/include" \
  -I"$root_dir/components/zectrix_connectivity/include" \
  -I"$root_dir/components/zectrix_companion/include" \
  -I"$reader_dir/include" -I"$reader_dir/private" -I"$reader_dir/third_party/miniz" \
  -I"$root_dir/components/zectrix_power/include" \
  -I"$root_dir/components/zectrix_self_test/include" \
  -I"$root_dir/components/zectrix_system/include" \
  -I"$root_dir/components/zectrix_time/include" \
  "$root_dir/components/zectrix_display/zectrix_display_state.cc" \
  "$root_dir/components/zectrix_display/zectrix_display_service.cc" \
  "$root_dir/components/zectrix_epd/zectrix_epd.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_canvas.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_demo_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_launcher_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_view_port.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_status_bar.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_reader_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_book_transfer_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_usb_manager_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_unicode_text.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_sleep_ui.cc" \
  "$root_dir/components/zectrix_app/zectrix_scene_manager.cc" \
  "$root_dir/components/zectrix_app/zectrix_app_contract.cc" \
  "$root_dir/components/zectrix_app/zectrix_application_runtime.cc" \
  "$root_dir/components/zectrix_app/zectrix_sdk_status.cc" \
  "$root_dir/components/zectrix_app/zectrix_launcher_controller.cc" \
  "$root_dir/components/zectrix_app/zectrix_first_party_app_controllers.cc" \
  "$root_dir/components/zectrix_app/zectrix_locale.cc" \
  "$root_dir/components/zectrix_app/zectrix_reader_controller.cc" \
  "$root_dir/components/zectrix_app/zectrix_book_transfer_controller.cc" \
  "$root_dir/components/zectrix_app/zectrix_sleep_cover.cc" \
  "$reader_dir/zectrix_reader.cc" "$reader_dir/zectrix_reader_zip.cc" \
  "$reader_dir/zectrix_reader_text.cc" "$reader_dir/zectrix_reader_font.cc" \
  "$reader_dir/zectrix_reader_bookmarks.cc" \
  "$root_dir/tools/display_service_test.cc" "$work_dir/inflate.o" "$work_dir/font.o" -o "$test_binary"
ZECTRIX_UI_LANGUAGE=en "$test_binary"
ZECTRIX_UI_LANGUAGE=zh "$test_binary"
echo 'PASS: display service, dirty regions, SSD2683 transfers and UI integration tests.'
