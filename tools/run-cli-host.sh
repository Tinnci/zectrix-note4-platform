#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(bash "$repo_root/tools/build-cli-host.sh")"
exec "$binary" "$@"
