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
    )
    for pattern in "${patterns[@]}"; do
        if rg -n -i --glob '*.{c,cc,cpp,h,hh,hpp}' "$pattern" "$scan_root"; then found=1; fi
    done
    return "$found"
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
        'const char* rtc = "PCF8563";' > "$temp_dir/bad/app.cc"
    if ! check_root "$temp_dir/good" || check_root "$temp_dir/bad"; then
        echo 'FAIL: architecture boundary checker self-test.' >&2
        return 1
    fi
    echo 'PASS: architecture boundary checker self-test.'
}

if [[ "${1:-}" == "--self-test" ]]; then run_self_test; exit 0; fi
status=0
found_root=0
for relative_root in main components/zectrix_demo_ui apps applications; do
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
