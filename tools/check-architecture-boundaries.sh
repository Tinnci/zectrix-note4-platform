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
        '#include[[:space:]]*[<"]host/ble_|#include[[:space:]]*[<"]nimble/'
        '#include[[:space:]]*[<"]esp_(bt|nimble|wifi|netif)[^">]*[>"]'
        '\bble_(gap|gatt|hs|sm)_[a-zA-Z0-9_]*[[:space:]]*\('
        '\besp_(wifi|netif)_[a-zA-Z0-9_]*[[:space:]]*\('
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

check_application_headers() {
    local scan_root="$1"
    rg -n --glob '*.{h,hh,hpp}' \
        '#include[[:space:]]*[<"]freertos/|\b(TaskHandle_t|QueueHandle_t|SemaphoreHandle_t)\b' \
        "$scan_root"
}

is_infrastructure_component() {
    case "$1" in
        zectrix_board|zectrix_companion|zectrix_connectivity|zectrix_display|zectrix_epd|zectrix_input|zectrix_nfc_service|zectrix_platform|\
        zectrix_power|zectrix_self_test|zectrix_storage|zectrix_system|zectrix_time)
            return 0
            ;;
    esac
    return 1
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
        '#include "host/ble_gap.h"' \
        '#include "esp_wifi.h"' \
        'void radio() { ble_gap_adv_start(0, nullptr, 0, nullptr, nullptr, nullptr); esp_wifi_start(); }' \
        'ZectrixBoard second_board;' \
        'const char* rtc = "PCF8563";' > "$temp_dir/bad/app.cc"
    printf '%s\n' \
        '#include "freertos/task.h"' \
        'TaskHandle_t product_architecture;' > "$temp_dir/bad/app.h"
    if ! check_root "$temp_dir/good" || check_root "$temp_dir/bad" ||
        check_application_headers "$temp_dir/good" ||
        ! check_application_headers "$temp_dir/bad"; then
        echo 'FAIL: architecture boundary checker self-test.' >&2
        return 1
    fi
    if ! is_infrastructure_component zectrix_platform || \
        is_infrastructure_component zectrix_new_application; then
        echo 'FAIL: Infrastructure allowlist self-test.' >&2
        return 1
    fi
    echo 'PASS: architecture boundary checker self-test.'
}

if [[ "${1:-}" == "--self-test" ]]; then run_self_test; exit 0; fi
status=0
found_root=0
declare -A scan_roots=(
    [main]=1
    [apps]=1
    [applications]=1
)

# Scan every first-party component unless it is an explicitly listed platform
# implementation component. Discovery does not depend on CMake dependency text.
while IFS= read -r component_dir; do
    component_name=${component_dir##*/}
    if is_infrastructure_component "$component_name"; then
        continue
    fi
    relative_root=${component_dir#"$root_dir/"}
    scan_roots["$relative_root"]=1
done < <(find "$root_dir/components" -mindepth 1 -maxdepth 1 -type d -print)

for relative_root in "${!scan_roots[@]}"; do
    scan_root="$root_dir/$relative_root"
    if [[ -d "$scan_root" ]]; then
        found_root=1
        echo "Checking application boundary: $relative_root"
        check_root "$scan_root" || status=1
        if check_application_headers "$scan_root"; then status=1; fi
    fi
done
if [[ "$found_root" == 0 ]]; then echo 'No application root exists.'; fi
if [[ "$status" != 0 ]]; then echo 'FAIL: application boundary check.' >&2; exit 1; fi
echo 'PASS: architecture boundary check.'
