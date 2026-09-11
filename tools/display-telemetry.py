#!/usr/bin/env python3
"""Convert `display telemetry` terminal captures into one CSV row per frame.

Run with uv: uv run --no-project tools/display-telemetry.py capture.txt -o frames.csv
"""

import argparse
import csv
import re
import sys
from pathlib import Path

COLUMNS = {
    "frame": (
        "sequence", "started_us", "kind", "reason", "error", "flags", "x", "y", "width", "height",
        "black_to_white", "white_to_black", "duration_us", "busy_us", "refresh_busy_us", "spi_bytes",
        "ram_bytes", "waveform_triggers",
    ),
    "env": (
        "sequence", "temperature_centi_c", "temperature_age_ms", "battery_mv", "battery_age_ms",
        "panel_temperature_centi_c", "gain_q8",
    ),
    "debt": (
        "sequence", "model_revision", "projected_mean_q16", "projected_peak_q16",
        "committed_mean_q16", "committed_peak_q16", "energy_uj",
    ),
}
FIELDNAMES = list(COLUMNS["frame"]) + list(COLUMNS["env"][1:]) + list(COLUMNS["debt"][1:])
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
SUMMARY = re.compile(r"# epd next=(\d+) latest=(\d+) lost=(\d+) frames=(\d+)$")


def normalize(frame):
    """Leave unknown measurements empty; zero remains a real observation."""
    row = dict(frame)
    if not row["flags"] & 1:
        row["black_to_white"] = row["white_to_black"] = ""
    if not row["flags"] & 2:
        for key in ("busy_us", "refresh_busy_us", "spi_bytes", "ram_bytes", "waveform_triggers"):
            row[key] = ""
    if not row["flags"] & 4:
        row["panel_temperature_centi_c"] = ""
    if not row["flags"] & 8:
        row["energy_uj"] = ""
    for sample, age in (("temperature_centi_c", "temperature_age_ms"), ("battery_mv", "battery_age_ms")):
        if row[age] == 0xFFFFFFFF:
            row[sample] = row[age] = ""
    return row


def parse_capture(lines):
    frames, losses = [], []
    pending, parts = None, set()
    found = False
    for number, raw in enumerate(lines, 1):
        line = ANSI.sub("", raw).strip()
        summary = SUMMARY.fullmatch(line)
        if summary:
            found = True
            losses.append(int(summary[3]))
            continue
        tag = line.split(",", 1)[0]
        if tag not in COLUMNS:
            continue
        found = True
        fields = next(csv.reader([line]))
        columns = COLUMNS[tag]
        if len(fields) != len(columns) + 1:
            raise ValueError(f"line {number}: incomplete {tag} row")
        try:
            values = dict(zip(columns, map(int, fields[1:])))
        except ValueError as error:
            raise ValueError(f"line {number}: non-integer {tag} value") from error
        if tag == "frame":
            if pending is not None:
                raise ValueError(f"line {number}: incomplete frame {pending['sequence']}")
            pending, parts = values, {tag}
        else:
            if pending is None or values["sequence"] != pending["sequence"] or tag in parts:
                raise ValueError(f"line {number}: unmatched or repeated {tag} row")
            pending.update(values)
            parts.add(tag)
        if parts == set(COLUMNS):
            frames.append(normalize(pending))
            pending, parts = None, set()
    if pending is not None:
        raise ValueError(f"incomplete frame {pending['sequence']}; capture all three rows")
    if not found:
        raise ValueError("no display telemetry found in capture")
    return frames, losses


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", type=Path, help="terminal capture; defaults to stdin")
    parser.add_argument("-o", "--output", type=Path, help="CSV destination; defaults to stdout")
    args = parser.parse_args()
    try:
        if args.capture:
            with args.capture.open(encoding="utf-8") as stream:
                frames, losses = parse_capture(stream)
        else:
            frames, losses = parse_capture(sys.stdin)
        stream = args.output.open("w", newline="", encoding="utf-8") if args.output else sys.stdout
        try:
            writer = csv.DictWriter(stream, FIELDNAMES)
            writer.writeheader()
            writer.writerows(frames)
        finally:
            if args.output:
                stream.close()
    except (OSError, ValueError) as error:
        parser.exit(1, f"display telemetry: {error}\n")
    print(f"Exported {len(frames)} frame(s).", file=sys.stderr)
    for lost in losses:
        if lost:
            print(f"Recorder reported {lost} overwritten frame(s) before a captured batch.", file=sys.stderr)


if __name__ == "__main__":
    main()
