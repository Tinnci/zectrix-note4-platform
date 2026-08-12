#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_include="$repo_root/components/zectrix_app/include"

expected_headers=$(printf '%s\n' \
    zectrix/sdk/application.h \
    zectrix/sdk/input.h \
    zectrix/sdk/status.h \
    zectrix/sdk/version.h \
    zectrix/zectrix_sdk.h | sort)

check_forbidden_tokens() {
    local root="$1"
    rg -n --glob '*.{h,hpp}' \
        '#include[[:space:]]*[<"](freertos/|driver/|esp_)|\b(esp_err_t|TickType_t|TaskHandle_t|QueueHandle_t|SemaphoreHandle_t|EventGroupHandle_t|ZectrixBoard|zectrix::Platform)\b' \
        "$root"
}

run_self_test() {
    local temp_dir
    temp_dir=$(mktemp -d)
    trap 'rm -rf "$temp_dir"' RETURN
    mkdir -p "$temp_dir/good" "$temp_dir/bad"
    printf '%s\n' '#include <cstdint>' 'struct Good { std::uint8_t value; };' \
        > "$temp_dir/good/sdk.h"
    printf '%s\n' '#include "freertos/task.h"' 'TaskHandle_t leaked;' \
        > "$temp_dir/bad/sdk.h"
    if check_forbidden_tokens "$temp_dir/good" ||
       ! check_forbidden_tokens "$temp_dir/bad"; then
        echo 'FAIL: SDK v1 checker self-test.' >&2
        return 1
    fi
    echo 'PASS: SDK v1 checker self-test.'
}

if [[ "${1:-}" == "--self-test" ]]; then
    run_self_test
    exit 0
fi

actual_headers=$(find "$sdk_include/zectrix" \
    -type f \( -name '*.h' -o -name '*.hpp' \) \
    -printf '%P\n' | sed 's#^#zectrix/#' | sort -u)

if [[ "$actual_headers" != "$expected_headers" ]]; then
    echo 'FAIL: SDK v1 public header set changed.' >&2
    diff -u <(printf '%s\n' "$expected_headers") \
            <(printf '%s\n' "$actual_headers") || true
    exit 1
fi

if check_forbidden_tokens "$sdk_include/zectrix/sdk" ||
   check_forbidden_tokens "$sdk_include/zectrix/zectrix_sdk.h"; then
    echo 'FAIL: SDK v1 exposes an implementation-specific token.' >&2
    exit 1
fi

echo 'PASS: SDK v1 public boundary.'
