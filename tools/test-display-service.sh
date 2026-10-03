#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=$(mktemp -d)
test_binary="$work_dir/display_service_test"
trap 'rm -rf "$work_dir"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [ "${NOTE4_DISPLAY_SANITIZE:-0}" = 1 ]; then
  flags+=(-O1 -g "-fsanitize=address,undefined" -fno-omit-frame-pointer)
fi
reader_dir="$root_dir/components/note4_reader"
uv run --no-project "$root_dir/tools/generate-reader-fixtures.py" "$work_dir/fixtures"
"${CC:-cc}" -std=c99 -I"$reader_dir/third_party/miniz" \
  -c "$reader_dir/third_party/miniz/miniz_tinfl.c" -o "$work_dir/inflate.o"
"${CC:-cc}" -DNOTE4_READER_FONT_PATH="\"$reader_dir/font/reader_font.bin\"" \
  -c "$reader_dir/note4_reader_font_data.S" -o "$work_dir/font.o"
"${CXX:-c++}" "${flags[@]}" \
  -I"$root_dir/tools/epd_host_include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/note4_display/include" \
  -I"$root_dir/components/note4_display/private_include" \
  -I"$root_dir/components/note4_epd/include" \
  -I"$root_dir/components/note4_epd/private_include" \
  -I"$root_dir/components/ui/include" \
  -I"$root_dir/components/ui/font" \
  -I"$root_dir/components/note4_app/include" -I"$root_dir/components/note4_text/include" \
  -I"$root_dir/components/note4_runtime/include" \
  -I"$root_dir/components/note4_host/include" \
  -I"$root_dir/components/note4_storage/include" \
  -I"$root_dir/components/note4_board/include" \
  -I"$root_dir/components/note4_connectivity/include" \
  -I"$root_dir/components/note4_companion/include" \
  -I"$reader_dir/include" -I"$reader_dir/private" -I"$reader_dir/third_party/miniz" \
  -I"$root_dir/components/note4_power/include" \
  -I"$root_dir/components/note4_self_test/include" \
  -I"$root_dir/components/note4_system/include" \
  -I"$root_dir/components/note4_time/include" \
  "$root_dir/components/note4_display/note4_display_state.cc" \
  "$root_dir/components/note4_display/note4_display_physics.cc" \
  "$root_dir/components/note4_display/note4_display_service.cc" \
  "$root_dir/components/note4_epd/note4_epd.cc" \
  "$root_dir/components/ui/canvas.cc" \
  "$root_dir/components/ui/ui_engine.cc" \
  "$root_dir/components/ui/launcher_ui.cc" \
  "$root_dir/components/ui/view_port.cc" \
  "$root_dir/components/ui/status_bar.cc" \
  "$root_dir/components/ui/reader_ui.cc" \
  "$root_dir/components/ui/book_transfer_ui.cc" \
  "$root_dir/components/ui/usb_manager_ui.cc" \
  "$root_dir/components/ui/micro_app_ui.cc" \
  "$root_dir/components/ui/micro_app_view.cc" \
  "$root_dir/components/ui/unicode_text.cc" \
  "$root_dir/components/ui/sleep_ui.cc" \
  "$root_dir/components/ui/utilities_ui.cc" \
  "$root_dir/components/note4_app/note4_scene_manager.cc" \
  "$root_dir/components/note4_app/note4_app_contract.cc" \
  "$root_dir/components/note4_app/note4_application_runtime.cc" \
  "$root_dir/components/note4_app/note4_sdk_status.cc" \
  "$root_dir/components/note4_app/note4_launcher_controller.cc" \
  "$root_dir/components/note4_app/note4_first_party_app_controllers.cc" \
  "$root_dir/components/note4_app/note4_locale.cc" \
  "$root_dir/components/note4_app/note4_reader_controller.cc" \
  "$root_dir/components/note4_app/note4_book_transfer_controller.cc" \
  "$root_dir/components/note4_app/note4_sleep_cover.cc" \
  "$root_dir/components/note4_app/note4_utilities.cc" \
  "$reader_dir/note4_reader.cc" "$reader_dir/note4_reader_zip.cc" \
  "$reader_dir/note4_reader_text.cc" "$reader_dir/note4_reader_font.cc" \
  "$reader_dir/note4_reader_bookmarks.cc" \
  "$root_dir/tools/display_service_test.cc" "$work_dir/inflate.o" "$work_dir/font.o" -o "$test_binary"
NOTE4_READER_FIXTURES="$work_dir/fixtures" NOTE4_UI_LANGUAGE=en "$test_binary"
NOTE4_READER_FIXTURES="$work_dir/fixtures" NOTE4_UI_LANGUAGE=zh "$test_binary"
echo 'PASS: display service, dirty regions, SSD2683 transfers and UI integration tests.'
