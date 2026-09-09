#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT

for profile in full core offline web ble; do
    connectivity=0 reader=0 transfer=0
    case "$profile" in
        full) connectivity=1 reader=1 transfer=1 ;;
        offline) reader=1 ;;
        web) connectivity=1 transfer=1 ;;
        ble) connectivity=1 ;;
    esac
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
        -DCONFIG_ZECTRIX_ENABLE_CONNECTIVITY="$connectivity" \
        -DCONFIG_ZECTRIX_ENABLE_READER="$reader" \
        -DCONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER="$transfer" \
        -I"$repo_root/tools/host_include" \
        -I"$repo_root/components/zectrix_app/include" \
        "$repo_root/components/zectrix_app/zectrix_first_party_app_controllers.cc" \
        "$repo_root/tools/first_party_app_controllers_test.cc" \
        -o "$test_binary"
    "$test_binary"
    printf 'PASS: first-party application controllers profile=%s.\n' "$profile"
done
