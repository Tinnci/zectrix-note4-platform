#!/usr/bin/env bash
# Flash the complete image set and verify a fresh six-second boot capture.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
port="${ZECTRIX_PORT:-/dev/cu.usbmodem14301}"
if [ ! -e "$port" ]; then
    printf 'FAIL: USB hardware device not found at %s\n' "$port" >&2
    exit 1
fi

if ! command -v idf.py >/dev/null 2>&1 || [ -z "${IDF_PATH:-}" ]; then
    # shellcheck disable=SC1091
    source "$repo_dir/tools/activate-dev-env.sh"
fi
command -v idf.py >/dev/null
printf '=== Zectrix Device Smoke Test ===\nHardware Port: %s\n' "$port"
printf 'Building firmware...\n'
bash "$repo_dir/tools/build-firmware.sh"
printf 'Flashing firmware to %s...\n' "$port"
idf.py --ccache -p "$port" flash
printf 'Capturing serial boot logs for 6 seconds...\n'
python3 - "$port" <<'PYEOF'
import re
import sys
import time

import serial
from esptool.reset import HardReset

try:
    with serial.Serial(sys.argv[1], 115200, timeout=0.2) as ser:
        ser.reset_input_buffer()
        # Reset after opening the port so the boot evidence is not lost during flash.
        ser.dtr = False
        HardReset(ser, uses_usb=True)()
        deadline = time.monotonic() + 6.0
        captured = bytearray()
        while time.monotonic() < deadline:
            captured.extend(ser.read(ser.in_waiting or 1))
except (OSError, serial.SerialException) as error:
    print(f"FAIL: Serial boot capture failed: {error}", file=sys.stderr)
    sys.exit(1)

output = captured.decode("utf-8", errors="replace")
print(output, end="" if output.endswith("\n") else "\n")
checks = {
    "bootloader": "boot: ESP-IDF" in output,
    "8 MiB PSRAM": "Found 8MB PSRAM device" in output,
    "Octal PSRAM": "octal_psram:" in output,
    "eFuse revision": "efuse block revision:" in output,
    "partition table": all(re.search(rf"boot:.*\b{name}\b", output)
                           for name in ("factory", "ota_0", "ota_1", "otadata")),
    "application initialization": "heap M2-equivalent platform:" in output,
    "application runtime": "heap M3 runtime active:" in output,
}
for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")
fatal = re.search(r"Guru Meditation|panic'ed|abort\(\) was called| E \(|^E \(", output, re.M)
if fatal or not all(checks.values()):
    print("FAIL: Device boot verification incomplete or startup error observed.")
    sys.exit(1)
print("PASS: Device hardware flash and boot verified.")
PYEOF
