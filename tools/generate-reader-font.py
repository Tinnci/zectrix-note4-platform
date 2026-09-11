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


def read_glyphs(path):
    opener = gzip.open if path.suffix == ".gz" else open
    glyphs = {}
    with opener(path, "rt", encoding="utf-8") as source:
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
    return b"".join(glyphs.get(code, fallback)
                    for first, last in RANGES for code in range(first, last + 1))


def pack_glyphs(raw):
    """Store width bits, four little-endian tile IDs per glyph, then 8x8 tiles."""
    if not raw or len(raw) % 33:
        raise ValueError("Expected width + 32 bitmap bytes per glyph")
    widths = bytearray((len(raw) // 33 + 7) // 8)
    indices = bytearray()
    tiles = {}
    for index, offset in enumerate(range(0, len(raw), 33)):
        glyph = raw[offset:offset + 33]
        if glyph[0] not in (8, 16):
            raise ValueError("Only 8px and 16px advances are supported")
        if glyph[0] == 16:
            widths[index // 8] |= 1 << (index % 8)
        for row in (0, 8):
            for column in (0, 1):
                tile = glyph[1 + row * 2 + column:1 + (row + 8) * 2 + column:2]
                tile_id = tiles.setdefault(tile, len(tiles))
                if tile_id > 0xFFFF:
                    raise ValueError("Font exceeds the 16-bit tile index capacity")
                indices.extend(tile_id.to_bytes(2, "little"))
    return widths + indices + b"".join(tiles)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--raw-output", type=Path, help="Optional unpacked glyphs for pixel comparison")
    args = parser.parse_args()
    raw = read_glyphs(args.source)
    packed = pack_glyphs(raw)
    args.output.write_bytes(packed)
    if args.raw_output:
        args.raw_output.write_bytes(raw)
    print(f"Wrote {len(raw) // 33} glyphs, {len(packed)} bytes "
          f"({len(raw) - len(packed)} bytes saved) to {args.output}")


if __name__ == "__main__":
    main()
