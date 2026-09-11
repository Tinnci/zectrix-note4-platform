"""Report firmware, linked font and Flash capacity from ESP-IDF build artifacts."""

import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


def read_partitions(path):
    data = path.read_bytes()
    entry = struct.Struct("<HBBII16sI")
    if not data or len(data) % entry.size:
        raise ValueError("Incomplete ESP-IDF partition table")
    partitions = []
    for offset in range(0, len(data), entry.size):
        magic, kind, subtype, address, size, label, flags = entry.unpack_from(data, offset)
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            # IDF already verifies its native table checksum during generation.
            continue
        if magic != 0x50AA:
            raise ValueError("Invalid ESP-IDF partition entry")
        partitions.append({"name": label.split(b"\0", 1)[0].decode("ascii"),
                           "type": kind, "subtype": subtype, "offset": address,
                           "size": size, "flags": flags})
    if not partitions:
        raise ValueError("Empty partition table")
    return partitions


def json_file(path):
    return json.loads(path.read_text())


def boot_partition_matches(output, partition):
    return re.search(rf"boot:.*\b{re.escape(partition['name'])}\b.*\b"
                     rf"{partition['offset']:08x}\s+{partition['size']:08x}\b",
                     output, re.I) is not None


def linked_font_bytes(directory, description):
    if "zectrix_reader" not in description["build_components"]:
        return 0
    cache = (directory / "CMakeCache.txt").read_text()
    match = re.search(r"^CMAKE_NM:FILEPATH=(.+)$", cache, re.MULTILINE)
    if not match:
        raise ValueError("Build has no configured symbol reader")
    symbols = subprocess.run([match[1], "-P", "-S", "--defined-only",
                              str(directory / description["app_elf"])],
                             check=True, capture_output=True, text=True).stdout
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[0] == "zectrix_reader_font_data":
            return int(fields[3], 16)
    raise ValueError("Reader build has no sized font symbol")


def inspect_budget(directory):
    description = json_file(directory / "project_description.json")
    flash = json_file(directory / "flasher_args.json")
    config = json_file(directory / "config/sdkconfig.json")
    capacity = re.fullmatch(r"(\d+)MB", flash["flash_settings"]["flash_size"])
    if not capacity:
        raise ValueError("Build must declare its Flash capacity")
    flash_bytes = int(capacity[1]) * 1024 * 1024
    table = flash["partition-table"]
    partitions = read_partitions(directory / table["file"])
    cursor = int(table["offset"], 0) + 0x1000
    unallocated = []
    for partition in sorted(partitions, key=lambda p: p["offset"]):
        start, size = partition["offset"], partition["size"]
        if size <= 0 or start < cursor or start + size > flash_bytes:
            raise ValueError("Partition exceeds Flash or overlaps reserved data")
        if cursor < start:
            unallocated.append({"offset": cursor, "size": start - cursor})
        cursor = start + size
    if cursor < flash_bytes:
        unallocated.append({"offset": cursor, "size": flash_bytes - cursor})
    image_bytes = (directory / description["app_bin"]).stat().st_size
    slots = [{"name": p["name"], "offset": p["offset"], "size": p["size"],
              "free_bytes": p["size"] - image_bytes}
             for p in partitions if p["type"] == 0]
    if not slots or image_bytes == 0:
        raise ValueError("Build has no application image or slots")
    minimum_slot = min(p["size"] for p in slots)
    optimization = next((name.lower() for name in ("SIZE", "PERF", "DEBUG", "NONE")
                         if config.get(f"COMPILER_OPTIMIZATION_{name}")), "unknown")
    return {"target": description["target"], "optimization": optimization,
            "application_bytes": image_bytes, "minimum_slot_bytes": minimum_slot,
            "application_free_bytes": minimum_slot - image_bytes,
            "application_free_percent": 100 * (minimum_slot - image_bytes) / minimum_slot,
            "reader_font_bytes": linked_font_bytes(directory, description),
            "configured_flash_bytes": flash_bytes, "application_slots": slots,
            "partitions": partitions, "unallocated_regions": unallocated,
            "unallocated_bytes": sum(region["size"] for region in unallocated)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = inspect_budget(args.build_dir.resolve())
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Firmware budget: {report['application_bytes']:,} bytes; "
              f"smallest slot {report['minimum_slot_bytes']:,}; "
              f"free {report['application_free_bytes']:,} ({report['application_free_percent']:.1f}%).")
        print(f"Linked reader font: {report['reader_font_bytes']:,} bytes; "
              f"optimization: {report['optimization']}; "
              f"unallocated Flash: {report['unallocated_bytes']:,} bytes.")
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"Cannot read firmware budget: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
