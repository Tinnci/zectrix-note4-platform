#!/usr/bin/env bash
set -euo pipefail
root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/tools/epd_host_include" \
  -I"$root_dir/tools/host_include" \
  -I"$root_dir/components/zectrix_display/include" \
  -I"$root_dir/components/zectrix_epd/include" \
  -I"$root_dir/components/zectrix_epd/private_include" \
  -I"$root_dir/components/zectrix_demo_ui/include" \
  -I"$root_dir/components/zectrix_demo_ui/font" \
  -I"$root_dir/components/zectrix_power/include" \
  -I"$root_dir/components/zectrix_self_test/include" \
  -I"$root_dir/components/zectrix_system/include" \
  -I"$root_dir/components/zectrix_time/include" \
  "$root_dir/components/zectrix_display/zectrix_display_state.cc" \
  "$root_dir/components/zectrix_display/zectrix_display_service.cc" \
  "$root_dir/components/zectrix_epd/zectrix_epd.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_canvas.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_demo_ui.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_view_port.cc" \
  "$root_dir/components/zectrix_demo_ui/zectrix_status_bar.cc" \
  "$root_dir/tools/display_service_test.cc" -o "$test_binary"
"$test_binary"
echo 'PASS: display service, dirty regions, SSD2683 transfers and UI integration tests.'
