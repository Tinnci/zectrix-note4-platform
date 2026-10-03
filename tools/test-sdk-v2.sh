#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary=$(mktemp)
example_object=$(mktemp)
trap 'rm -f "$test_binary" "$example_object"' EXIT

"$repo_root/tools/check-sdk-v2.sh" --self-test
"$repo_root/tools/check-sdk-v2.sh"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/tools/host_include" \
    -I"$repo_root/components/note4_app/include" -I"$repo_root/components/note4_text/include" \
    "$repo_root/components/note4_app/note4_app_contract.cc" \
    "$repo_root/components/note4_app/note4_application_runtime.cc" \
    "$repo_root/components/note4_app/note4_sdk_status.cc" \
    "$repo_root/tools/sdk_v2_consumer_test.cc" \
    -o "$test_binary"
"$test_binary"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/components/note4_app/include" -I"$repo_root/components/note4_text/include" \
    -c "$repo_root/examples/sdk_v2_minimal_app.cc" \
    -o "$example_object"

echo 'PASS: SDK v2 locked consumer and example.'
