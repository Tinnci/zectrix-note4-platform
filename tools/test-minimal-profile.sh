#!/usr/bin/env bash
# Run the Host composition matrix, build both products and compare real artifacts.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
device=0
usage() { printf 'Usage: %s [--device]\n' "$0"; }
case "${1:-}" in
    --device) device=1; shift ;;
    --help|-h) usage; exit 0 ;;
esac
if [ "$#" -ne 0 ]; then usage >&2; exit 2; fi
cd "$repo_dir"
report_dir="$repo_dir/build-profile-regression"
mkdir -p "$report_dir"
# A failed rerun must not leave a previous success report looking current.
rm -f "$report_dir/report.json" "$report_dir/full-smoke.log" "$report_dir/minimal-smoke.log"

# The suite already compiles Full/Minimal Platform compositions and tests
# optional components independently, so running it twice would repeat work.
bash tools/test-host.sh 2>&1 | tee "$report_dir/host.log"
# A caller's virtualenv may shadow IDF's Python even when idf.py is on PATH.
# shellcheck disable=SC1091
source "$repo_dir/tools/activate-dev-env.sh"
for profile in full minimal; do
    bash tools/build-firmware.sh --profile "$profile" 2>&1 | tee "$report_dir/$profile-build.log"
done
uv run --no-project tools/compare-firmware-profiles.py build-full build-minimal \
    --output "$report_dir/report.json" 2>&1 | tee "$report_dir/comparison.log"

if [ "$device" -eq 1 ]; then
    port="${ZECTRIX_PORT:-/dev/cu.usbmodem14301}"
    if [ -e "$port" ]; then
        # Finish on Full so a successful run leaves the complete product installed.
        for profile in minimal full; do
            bash tools/device-smoke-test.sh --profile "$profile" 2>&1 | tee "$report_dir/$profile-smoke.log"
        done
    else
        printf 'SKIP: Full/Minimal device smoke; no hardware at %s.\n' "$port"
    fi
else
    printf 'SKIP: device smoke; use --device with a connected ESP32-S3.\n'
fi
printf 'PASS: Full/Minimal Host and firmware regression. Results: %s\n' "$report_dir"
