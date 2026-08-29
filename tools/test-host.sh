#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host_tests=(
    test-app-contract.sh
    test-application-runtime.sh
    test-cli-core.sh
    test-companion-identity.sh
    test-companion-protocol.sh
    test-connectivity-policy.sh
    test-display-service.sh
    test-display-state.sh
    test-enrollment-ndef.sh
    test-first-party-app-controllers.sh
    test-input-event.sh
    test-pairing-bootstrap.sh
    test-platform.sh
    test-power-service.sh
    test-sdk-v1.sh
    test-storage-service.sh
    test-sync-engine.sh
    test-system-service.sh
    test-time-service.sh
)

"$repo_root/tools/check-architecture-boundaries.sh" --self-test
"$repo_root/tools/check-architecture-boundaries.sh"

for test_script in "${host_tests[@]}"; do
    "$repo_root/tools/$test_script"
done

printf 'PASS: host suite tests=%d.\n' "${#host_tests[@]}"
