#!/usr/bin/env bash
# Flash the complete image set and verify a fresh six-second boot capture.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile=""
usage() { printf 'Usage: %s [--profile full|minimal]\n' "$0"; }
case "${1:-}" in
    --profile)
        case "${2:-}" in
            full|minimal) profile="$2"; shift 2 ;;
            *) usage >&2; exit 2 ;;
        esac ;;
    --help|-h) usage; exit 0 ;;
esac
if [ "$#" -ne 0 ]; then usage >&2; exit 2; fi
build_dir="$repo_dir/build${profile:+-$profile}"
build_args=("$repo_dir/tools/build-firmware.sh")
if [ -n "$profile" ]; then build_args+=(--profile "$profile"); fi
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
printf '=== Zectrix Device Smoke Test ===\nHardware Port: %s; profile: %s\n' "$port" "${profile:-local}"
printf 'Building firmware...\n'
bash "${build_args[@]}"
printf 'Flashing firmware to %s...\n' "$port"
idf.py --ccache -B "$build_dir" -p "$port" flash
printf 'Capturing serial boot logs for 6 seconds...\n'
python3 - "$port" "$build_dir" <<'PYEOF'
import json
import re
import sys
import time
from pathlib import Path

import serial
from esptool.reset import HardReset

build_dir = Path(sys.argv[2])
config = json.loads((build_dir / "config/sdkconfig.json").read_text())
# Count the mandatory catalog and the service-backed optional destinations.
expected_apps = 9 + sum(bool(config.get(f"ZECTRIX_ENABLE_{module}"))
                        for module in ("CONNECTIVITY", "READER", "BOOK_TRANSFER", "USB_HOST", "RUNTIME"))
try:
    with serial.Serial(sys.argv[1], 115200, timeout=0.2) as ser:
        ser.reset_input_buffer()
        # Reset after opening the port so the boot evidence is not lost during flash.
        ser.dtr = False
        HardReset(ser, uses_usb=True)()
        deadline = time.monotonic() + 6.0
        captured = bytearray()
        following = False
        while time.monotonic() < deadline:
            captured.extend(ser.read(ser.in_waiting or 1))
            if not following and b"zectrix> " in captured:
                # The CLI owns log output after platform initialization.
                ser.write(b"log follow debug\r")
                following = True
        if following:
            ser.write(b"\x03")
except (OSError, serial.SerialException) as error:
    print(f"FAIL: Serial boot capture failed: {error}", file=sys.stderr)
    sys.exit(1)

output = captured.decode("utf-8", errors="replace")
(build_dir / "device-smoke.log").write_text(output)
print(output, end="" if output.endswith("\n") else "\n")
ready = re.search(r"launcher ready: applications=(\d+)\b", output)
checks = {
    "bootloader": "boot: ESP-IDF" in output,
    "8 MiB PSRAM": "Found 8MB PSRAM device" in output,
    "Octal PSRAM": "octal_psram:" in output,
    "eFuse revision": "efuse block revision:" in output,
    "partition table": all(re.search(rf"boot:.*\b{name}\b", output)
                           for name in ("factory", "ota_0", "ota_1", "otadata", "books")),
    "application initialization": "heap M2-equivalent platform:" in output,
    "application runtime": "heap M3 runtime active:" in output,
    "first Launcher frame and boot confirmation": ready is not None,
    "selected application catalog": ready is not None and int(ready[1]) == expected_apps,
    "selected USB CLI": following == bool(config.get("ZECTRIX_ENABLE_USB_CLI")),
}
for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")
fatal = re.search(r"Guru Meditation|panic'ed|abort\(\) was called| E \(|^E \(", output, re.M)
if fatal or not all(checks.values()):
    print("FAIL: Device boot verification incomplete or startup error observed.")
    sys.exit(1)
print("PASS: Device hardware flash and boot verified.")
PYEOF
