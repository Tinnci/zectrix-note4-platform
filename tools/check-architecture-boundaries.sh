#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

check_root() {
    local scan_root="$1" found=0 pattern
    local patterns=(
        '#include[[:space:]]*[<"]driver/gpio\.h[>"]'
        '#include[[:space:]]*[<"]driver/spi_master\.h[>"]'
        '#include[[:space:]]*[<"]zectrix_epd\.h[>"]'
        'esp_deep_sleep_start[[:space:]]*\('
        'nvs_[a-zA-Z0-9_]*[[:space:]]*\('
        'PCF8563'
        'esp_timer_get_time[[:space:]]*\('
        'esp_app_get_description[[:space:]]*\('
        'esp_chip_info[[:space:]]*\('
        'esp_reset_reason[[:space:]]*\('
        'esp_flash_get_size[[:space:]]*\('
        'esp_read_mac[[:space:]]*\('
        'heap_caps_get_[a-zA-Z0-9_]*[[:space:]]*\('
        '(DisplayService|InputService|PowerService|TimeService|StorageService|SystemService)::(Attach|Create)[[:space:]]*\('
        'BoardForSelfTest[[:space:]]*\('
        '#include[[:space:]]*[<"]zectrix_board\.h[>"]'
        '\bZectrixBoard\b'
    )
    for pattern in "${patterns[@]}"; do
        if rg -n -i --glob '*.{c,cc,cpp,h,hh,hpp}' "$pattern" "$scan_root"; then found=1; fi
    done
    return "$found"
}

is_application_consumer() {
    rg -q 'zectrix_(display|input|power|time|storage|system|platform)' "$1"
}

run_self_test() {
    local temp_dir
    temp_dir=$(mktemp -d)
    trap 'rm -rf "$temp_dir"' RETURN
    mkdir -p "$temp_dir/good" "$temp_dir/bad"
    printf '%s\n' 'void app_entry(void) {}' > "$temp_dir/good/app.cc"
    printf '%s\n' \
        '#include "driver/gpio.h"' \
        '#include "driver/spi_master.h"' \
        '#include "zectrix_epd.h"' \
        'void sleep_now() { esp_deep_sleep_start(); }' \
        'void load() { nvs_get_i32(0, "key", nullptr); }' \
        'void identity() { esp_read_mac(nullptr, 0); }' \
        'void service() { InputService::Attach(board, nullptr); }' \
        '#include "zectrix_board.h"' \
        'ZectrixBoard second_board;' \
        'const char* rtc = "PCF8563";' > "$temp_dir/bad/app.cc"
    if ! check_root "$temp_dir/good" || check_root "$temp_dir/bad"; then
        echo 'FAIL: architecture boundary checker self-test.' >&2
        return 1
    fi
    printf '%s\n' 'idf_component_register(REQUIRES zectrix_platform)' > \
        "$temp_dir/platform-consumer.cmake"
    if ! is_application_consumer "$temp_dir/platform-consumer.cmake"; then
        echo 'FAIL: Platform consumer discovery self-test.' >&2
        return 1
    fi
    echo 'PASS: architecture boundary checker self-test.'
}

if [[ "${1:-}" == "--self-test" ]]; then run_self_test; exit 0; fi
status=0
found_root=0
declare -A application_roots=(
    [main]=1
    [components/zectrix_demo_ui]=1
    [apps]=1
    [applications]=1
)

# Discover current consumers from the build files. This prevents a new
# application component from bypassing the check only because its directory
# name is not in the seed list above.
while IFS= read -r cmake_file; do
    component_dir=${cmake_file%/CMakeLists.txt}
    component_name=${component_dir##*/}
    if [[ "$component_name" =~ ^zectrix_(display|input|power|time|storage|system|platform|self_test)$ ]]; then
        continue
    fi
    if is_application_consumer "$cmake_file"; then
        relative_root=${cmake_file#"$root_dir/"}
        application_roots["${relative_root%/CMakeLists.txt}"]=1
    fi
done < <(find "$root_dir" \
    -path "$root_dir/build" -prune -o \
    -path "$root_dir/managed_components" -prune -o \
    -name CMakeLists.txt -type f -print)

for relative_root in "${!application_roots[@]}"; do
    scan_root="$root_dir/$relative_root"
    if [[ -d "$scan_root" ]]; then
        found_root=1
        echo "Checking application boundary: $relative_root"
        check_root "$scan_root" || status=1
    fi
done
if [[ "$found_root" == 0 ]]; then echo 'No application root exists.'; fi
if [[ "$status" != 0 ]]; then echo 'FAIL: application boundary check.' >&2; exit 1; fi
echo 'PASS: architecture boundary check.'
