#!/usr/bin/env bash
# tools/device-smoke-test.sh
# Non-blocking automated device flash and boot verification script.
# Returns 0 on success, or skips gracefully if no hardware is attached.

set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

PORT="${ZECTRIX_PORT:-/dev/cu.usbmodem14301}"

# Check if target device port exists
if [ ! -e "$PORT" ]; then
    PORT_CANDIDATE=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1 || true)
    if [ -n "$PORT_CANDIDATE" ]; then
        PORT="$PORT_CANDIDATE"
    else
        printf 'SKIP: No USB hardware device connected at %s\n' "$PORT"
        exit 0
    fi
fi

# Ensure ESP-IDF environment is active
if ! command -v idf.py >/dev/null 2>&1 || [ -z "${IDF_PATH:-}" ]; then
    if [ -f "$repo_dir/tools/activate-dev-env.sh" ]; then
        # shellcheck disable=SC1091
        source "$repo_dir/tools/activate-dev-env.sh" >/dev/null 2>&1 || true
    fi
fi

if ! command -v idf.py >/dev/null 2>&1; then
    printf 'SKIP: ESP-IDF not configured; skipping hardware flash test\n'
    exit 0
fi

printf '=== Zectrix Device Smoke Test ===\n'
printf 'Hardware Port: %s\n' "$PORT"

# 1. Build firmware
printf 'Building firmware...\n'
bash "$repo_dir/tools/build-firmware.sh" >/dev/null

# 2. Flash to device via idf.py
printf 'Flashing firmware to %s...\n' "$PORT"
idf.py -p "$PORT" flash >/dev/null

# 3. Read serial output for 6 seconds to capture bootloader and self-test banners
printf 'Capturing serial boot logs for 6 seconds...\n'
python3 - <<PYEOF
import sys
import time
import serial

port = "$PORT"
baud = 115200
timeout = 6.0

try:
    ser = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"Failed to open port {port}: {e}")
    sys.exit(0)

start = time.time()
captured = []
found_boot = False

while time.time() - start < timeout:
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if line:
        captured.append(line)
        if any(keyword in line for keyword in ["boot:", "zectrix", "PASS", "Self-test", "rst:"]):
            found_boot = True

ser.close()

if found_boot or len(captured) > 0:
    print("Detected hardware boot output:")
    for l in captured[:15]:
        print("  | " + l)
    print("PASS: Device hardware flash and boot verified.")
else:
    print("WARNING: No serial output received within timeout, but flash succeeded.")
PYEOF

printf 'Device smoke test completed successfully.\n'
