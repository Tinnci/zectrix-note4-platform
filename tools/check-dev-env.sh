#!/usr/bin/env bash
set -euo pipefail

expected_idf="${ZECTRIX_IDF_PATH:-/home/drie/esp/esp-idf-v5.5.2}"
expected_idf_tag="v5.5.2"
expected_idf_commit="30aaf64524299d3bde422ca9a2848090d1bc5d0f"
repo_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

required_submodules=(
    "components/esp_wifi/lib 01d52d9e69032c486015dc28b08c3bf6aaf348a9"
    "components/esp_phy/lib 3d57415af6e4c92eff2c4c3463e20a51d7340aba"
    "components/lwip/lwip fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e"
    "components/mbedtls/mbedtls ffb280bb63c78bfec1e1ab55040671768c85c923"
    "components/esp_coex/lib 63e292b57b2cda9f9496a71a04bec43e1f0caeba"
    "components/heap/tlsf 2867f6883a12920b1969ff9624c0ab0e4185c2ce"
    "components/bt/controller/lib_esp32c3_family 9b50531537e755792ac827d00d233eab499a0b37"
    "components/bt/host/nimble/nimble 9551ac31af0348d7de6cbe7527de3e5ba205460d"
    "components/unity/unity bf560290f6020737eafaa8b5cbd2177c3956c03f"
    "components/cmock/CMock eeecc49ce8af123cf8ad40efdb9673e37b56230f"
    "components/spiffs/spiffs 0dbb3f71c5f6fae3747a9d935372773762baf852"
    "components/protobuf-c/protobuf-c abc67a11c6db271bedbb9f58be85d6f4e2ea8389"
    "components/json/cJSON c859b25da02955fef659d658b8f324b5cde87be3"
    "components/bootloader/subproject/components/micro-ecc/micro-ecc 24c60e243580c7868f4334a1ba3123481fe1aa48"
)

pass_count=0
warn_count=0
fail_count=0

pass_check() { printf 'PASS %s\n' "$*"; pass_count=$((pass_count + 1)); }
warn_check() { printf 'WARN %s\n' "$*"; warn_count=$((warn_count + 1)); }
fail_check() { printf 'FAIL %s\n' "$*"; fail_count=$((fail_count + 1)); }

for command_name in gcc git make flex bison gperf python3 cmake ninja ccache jq rg curl gh; do
    if command -v "$command_name" >/dev/null 2>&1; then
        pass_check "command=$command_name path=$(command -v "$command_name")"
    else
        fail_check "missing command=$command_name"
    fi
done

if [ -f "$expected_idf/export.sh" ]; then
    pass_check "idf_path=$expected_idf"
    idf_tag="$(git -C "$expected_idf" describe --tags --exact-match 2>/dev/null || true)"
    if [ "$idf_tag" = "$expected_idf_tag" ]; then
        pass_check "idf_tag=$idf_tag"
    else
        fail_check "idf_tag expected=$expected_idf_tag actual=${idf_tag:-unknown}"
    fi
    idf_commit="$(git -C "$expected_idf" rev-parse HEAD)"
    if [ "$idf_commit" = "$expected_idf_commit" ]; then
        pass_check "idf_commit=$idf_commit"
    else
        fail_check "idf_commit expected=$expected_idf_commit actual=$idf_commit"
    fi
    if git -C "$expected_idf" diff --quiet --ignore-submodules; then
        pass_check "idf_superproject_clean=true"
    else
        warn_check "idf_superproject_dirty=true"
    fi
    incomplete_submodules="$(git -C "$expected_idf" submodule status 2>/dev/null | awk '$1 ~ /^[+-U]/ { count++ } END { print count + 0 }')"
    if [ "$incomplete_submodules" -eq 0 ]; then
        pass_check "idf_submodules_complete=true"
    else
        warn_check "idf_submodules_incomplete=$incomplete_submodules"
    fi
else
    fail_check "idf_path_missing=$expected_idf"
fi

for submodule_spec in "${required_submodules[@]}"; do
    read -r submodule_path expected_submodule_commit <<< "$submodule_spec"
    actual_submodule_commit="$(git -C "$expected_idf/$submodule_path" rev-parse --verify HEAD^{commit} 2>/dev/null || true)"
    if [ "$actual_submodule_commit" = "$expected_submodule_commit" ]; then
        pass_check "submodule=$submodule_path@$actual_submodule_commit"
    else
        fail_check "submodule=$submodule_path expected=$expected_submodule_commit actual=${actual_submodule_commit:-unknown}"
    fi
done

if [ -n "${IDF_PATH:-}" ] && [ "$IDF_PATH" = "$expected_idf" ]; then
    pass_check "active_idf_path=$IDF_PATH"
else
    warn_check "active_idf_path=${IDF_PATH:-unset} expected=$expected_idf"
fi

if command -v idf.py >/dev/null 2>&1; then
    idf_version="$(idf.py --version 2>/dev/null || true)"
    case "$idf_version" in
        *"5.5.2"*) pass_check "idf_version=$idf_version" ;;
        *) fail_check "idf_version expected=5.5.2 actual=${idf_version:-unknown}" ;;
    esac
else
    fail_check "missing command=idf.py"
fi

cmake_version="$(cmake --version 2>/dev/null | awk 'NR == 1 { print $3 }')"
cmake_major="${cmake_version%%.*}"
if [ -n "$cmake_major" ] && [ "$cmake_major" -lt 4 ]; then
    pass_check "cmake_version=$cmake_version"
else
    fail_check "cmake_version expected=<4 actual=${cmake_version:-unknown}"
fi

if gh auth status >/dev/null 2>&1; then pass_check "gh_auth=ok"; else fail_check "gh_auth=failed"; fi
if gh extension list 2>/dev/null | grep -q 'valeriobelli/gh-milestone.*v2.2.0'; then
    pass_check "gh_milestone=v2.2.0"
else
    fail_check "gh_milestone expected=v2.2.0"
fi

for url in https://github.com https://api.github.com https://components.espressif.com; do
    if curl -fsSI --max-time 15 "$url" >/dev/null 2>&1; then pass_check "network=$url"; else warn_check "network=$url unavailable"; fi
done

lock_file="$repo_dir/dependencies.lock"
if grep -q "version: 5.5.2" "$lock_file" && grep -q "version: 1.5.11" "$lock_file" && grep -q "target: esp32s3" "$lock_file"; then
    pass_check "dependencies_lock=5.5.2/1.5.11/esp32s3"
else
    fail_check "dependencies_lock does not match reference tuple"
fi

serial_count="$(find /dev -maxdepth 1 \( -name 'ttyACM*' -o -name 'ttyUSB*' \) -printf '.' 2>/dev/null | wc -c)"
if [ "$serial_count" -gt 0 ]; then pass_check "serial_devices=$serial_count"; else warn_check "serial_devices=0"; fi

printf 'SUMMARY pass=%s warn=%s fail=%s\n' "$pass_count" "$warn_count" "$fail_count"
test "$fail_count" -eq 0
