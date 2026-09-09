#!/usr/bin/env python3
"""Build the reader's 16px bitmap subset from GNU Unifont 15.1.05 BDF.

Run with uv run tools/generate-reader-font.py input.bdf[.gz] output.bin.
Font data is distributed under SIL OFL 1.1; see the reader font directory.
"""

import argparse
import gzip
from pathlib import Path


RANGES = ((0x20, 0x2FF), (0x2000, 0x206F), (0x3000, 0x30FF),
          (0x31F0, 0x31FF), (0x3400, 0x9FFF), (0xAC00, 0xD7A3),
          (0xFF00, 0xFFEF), (0xFFFD, 0xFFFD))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    opener = gzip.open if args.source.suffix == ".gz" else open
    glyphs = {}
    with opener(args.source, "rt", encoding="utf-8") as source:
        code = -1
        width = 16
        for line in source:
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "ENCODING":
                code = int(fields[1])
            elif fields[0] == "DWIDTH":
                width = int(fields[1])
            elif fields[0] == "BITMAP":
                rows = []
                for row in source:
                    if row.strip() == "ENDCHAR":
                        break
                    rows.append(row.strip())
                if len(rows) != 16 or width not in (8, 16):
                    continue
                bitmap = b"".join((int(row, 16) << (8 if width == 8 else 0)).to_bytes(2, "big")
                                   for row in rows)
                glyphs[code] = bytes([width]) + bitmap
    fallback = glyphs[0xFFFD]
    result = b"".join(glyphs.get(code, fallback)
                      for first, last in RANGES for code in range(first, last + 1))
    args.output.write_bytes(result)
    print(f"Wrote {len(result) // 33} glyphs, {len(result)} bytes to {args.output}")


if __name__ == "__main__":
    main()
