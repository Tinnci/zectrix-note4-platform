#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary=$(mktemp)
example_object=$(mktemp)
trap 'rm -f "$test_binary" "$example_object"' EXIT

"$repo_root/tools/check-sdk-v1.sh" --self-test
"$repo_root/tools/check-sdk-v1.sh"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/components/zectrix_app/include" \
    "$repo_root/components/zectrix_app/zectrix_app_contract.cc" \
    "$repo_root/components/zectrix_app/zectrix_application_runtime.cc" \
    "$repo_root/components/zectrix_app/zectrix_sdk_status.cc" \
    "$repo_root/tools/sdk_v1_consumer_test.cc" \
    -o "$test_binary"
"$test_binary"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/components/zectrix_app/include" \
    -c "$repo_root/examples/sdk_v1_minimal_app.cc" \
    -o "$example_object"

echo 'PASS: SDK v1 locked consumer and example.'
