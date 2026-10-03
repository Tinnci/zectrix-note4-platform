#!/usr/bin/env python3
"""Compile real canvas with each codec bundle using the existing target build flags."""
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=11,<13"]
# ///
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path
from digit_font import ROOT, CODECS

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "build-font-codecs")
    parser.add_argument("--recipe", type=Path, default=ROOT / "docs/design/date-digits/raster-settings.json")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    commands = json.loads((args.build / "compile_commands.json").read_text())
    entry = next(c for c in commands if Path(c["file"]).resolve() == ROOT / "components/ui/canvas.cc")
    original = entry.get("arguments") or shlex.split(entry["command"])
    compiler = next(Path(arg) for arg in original if Path(arg).name.endswith("g++"))
    nm = compiler.with_name(compiler.name.removesuffix("g++") + "nm")
    results = []
    for name, codecs in (("legacy", (0, 1)), ("xor", (0, 1, 2)), ("column", (0, 1, 3)), ("all", (0, 1, 2, 3))):
        directory = (args.output / name).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        header = directory / "zectrix_large_digits.h"
        subprocess.run([sys.executable, str(ROOT / "tools/generate-large-digits.py"), "--recipe", str(args.recipe),
                        "--output", str(header), "--preview", str(directory / "preview.png"),
                        "--codecs", *[CODECS[c] for c in codecs]], check=True)
        command = list(original)
        object_file = directory / "canvas.o"
        command[command.index("-o") + 1] = str(object_file)
        # Override only generated glyph data. Source and every IDF target flag remain unchanged.
        command.insert(command.index("-o"), "-I" + str(directory))
        # Include resolution must prefer this generated header over the regular font directory.
        include = command.pop(command.index("-I" + str(directory)))
        command.insert(command.index(str(compiler)) + 1, include)
        environment = {**os.environ, "CCACHE_DISABLE": "1"}
        subprocess.run(command, cwd=entry["directory"], env=environment, check=True)
        symbols = subprocess.check_output([str(nm), "-S", "-C", str(object_file)], text=True)
        sizes = []
        for line in symbols.splitlines():
            fields = line.split(maxsplit=3)
            if len(fields) == 4 and fields[2] in ("T", "t", "W", "w") and (
                    "LargeNumber" in fields[3] or "ZectrixDecodeDigit" in fields[3]):
                sizes.append({"symbol": fields[3], "bytes": int(fields[1], 16)})
        data = header.read_text()
        payload_bytes = len(re.findall(r"0x[0-9a-f]{2}", data))
        code_bytes = sum(s["bytes"] for s in sizes)
        result = {"bundle": name, "codecs": [CODECS[c] for c in codecs], "payload_bytes": payload_bytes,
                  "index_bytes": 300, "target_function_bytes": code_bytes,
                  "estimated_linked_bytes": payload_bytes + 300 + code_bytes, "symbols": sizes,
                  "note": "Actual ESP target object symbols; final ELF alignment/linker effects measured by firmware build."}
        results.append(result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
    winner = min(results, key=lambda r: r["estimated_linked_bytes"])
    report = {"bundles": results, "smallest_bundle": winner["bundle"],
              "limitations": ["No MCU latency/energy measurement", "Object symbol estimate excludes final link padding"]}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")

if __name__ == "__main__":
    main()
