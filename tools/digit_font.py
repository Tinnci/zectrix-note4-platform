"""Shared offline source extraction, rasterization and lossless digit codecs."""
from itertools import groupby
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ("a-editorial-serif.png", ((666, 900), (930, 1158))),
    ("b-rounded-geometric.png", ((698, 949), (977, 1209))),
    ("c-ink-calligraphic.png", ((642, 920), (954, 1210))),
    ("d-polka-dots.png", None), ("e-art-deco.png", None),
]
CODECS = ("raw", "rle", "xor_rle", "column_rle")

def source_glyphs():
    result = []
    for style, (name, bands) in enumerate(SOURCES):
        source = Image.open(ROOT / "docs/design/date-digits" / name).convert("L")
        bands = bands or ((0, source.height // 2), (source.height // 2, source.height))
        for top, bottom in bands:
            if style < 3:
                band = source.crop((0, top, source.width, bottom)).point(lambda p: 255 if p < 100 else 0)
                occupied = [band.crop((x, 0, x + 1, band.height)).getbbox() is not None for x in range(source.width)]
                columns, cursor = [], 0
                for on, group in groupby(occupied):
                    length = sum(1 for _ in group)
                    if on:
                        columns.append((cursor, cursor + length))
                    cursor += length
                if len(columns) != 5:
                    raise ValueError(f"Expected five numerals in {name}: {columns}")
            else:
                columns = [(round(i * source.width / 5), round((i + 1) * source.width / 5)) for i in range(5)]
            for left, right in columns:
                cell = source.crop((left, top, right, bottom))
                box = cell.point(lambda p: 255 if p < 100 else 0).getbbox()
                if box is None:
                    raise ValueError(f"Empty glyph: {name}")
                result.append(cell.crop(box))
    return result

def rasterize(source, height=48, threshold=150, method="lanczos"):
    width = min(round(42 * height / 48), max(1, round(source.width * height / source.height)))
    resampling = Image.Resampling.LANCZOS if method == "lanczos" else Image.Resampling.BOX
    return source.resize((width, height), resampling).point(lambda p: 255 if p >= threshold else 0, "1")

def trim(image):
    from PIL import ImageOps
    box = ImageOps.invert(image.convert("L")).getbbox()
    if box is None:
        raise ValueError("Empty rasterized glyph")
    return image.crop((box[0], 0, box[2], image.height))

def bits_of(image):
    return [int(value == 0) for value in image.convert("L").tobytes()]

def pack(bits):
    return bytes(sum(bit << (7 - i) for i, bit in enumerate(bits[j:j + 8])) for j in range(0, len(bits), 8))

def runs(bits):
    result = bytearray()
    for bit, group in groupby(bits):
        count = sum(1 for _ in group)
        while count:
            length = min(count, 128)
            result.append((bit << 7) | (length - 1))
            count -= length
    return bytes(result)

def encode_codec(bits, width, codec):
    if width < 1 or len(bits) % width or any(bit not in (0, 1) for bit in bits):
        raise ValueError("Invalid glyph bits or dimensions")
    if codec == 0:
        return pack(bits)
    if codec == 1:
        return runs(bits)
    if codec == 2:
        return runs([bit ^ (bits[i - width] if i >= width else 0) for i, bit in enumerate(bits)])
    if codec == 3:
        height = len(bits) // width
        return runs([bits[row * width + col] for col in range(width) for row in range(height)])
    raise ValueError("Unknown codec")

def decode(payload, width, height, codec):
    count = width * height
    if width < 1 or height < 1:
        raise ValueError("Invalid dimensions")
    if codec == 0:
        if len(payload) != (count + 7) // 8:
            raise ValueError("Invalid raw length")
        return [(byte >> shift) & 1 for byte in payload for shift in range(7, -1, -1)][:count]
    if codec not in (1, 2, 3):
        raise ValueError("Unknown codec")
    result = [bit for byte in payload for bit in [byte >> 7] * ((byte & 127) + 1)]
    if len(result) != count:
        raise ValueError("Invalid run length")
    if codec == 2:
        for i in range(width, count):
            result[i] ^= result[i - width]
    if codec == 3:
        result = [result[col * height + row] for row in range(height) for col in range(width)]
    return result

def choose(bits, width, codecs=(0, 1, 2, 3)):
    candidates = [(encode_codec(bits, width, codec), codec) for codec in codecs]
    payload, codec = min(candidates, key=lambda item: (len(item[0]), item[1]))
    if decode(payload, width, len(bits) // width, codec) != bits:
        raise ValueError("Codec roundtrip mismatch")
    return payload, codec, len(pack(bits))

def tile_pack(glyphs, tile_size=4):
    """One shared dictionary; uint16 tile IDs and width/height per glyph."""
    dictionary, lookup, indices, shapes = [], {}, [], []
    for image in glyphs:
        shapes.append((image.width, image.height))
        for y in range(0, image.height, tile_size):
            for x in range(0, image.width, tile_size):
                tile = Image.new("1", (tile_size, tile_size), 1)
                tile.paste(image.crop((x, y, min(x + tile_size, image.width), min(y + tile_size, image.height))), (0, 0))
                data = pack(bits_of(tile))
                if data not in lookup:
                    lookup[data] = len(dictionary)
                    dictionary.append(data)
                indices.append(lookup[data])
    return dictionary, indices, shapes

def tile_unpack(dictionary, indices, shapes, tile_size=4):
    result, cursor = [], 0
    for width, height in shapes:
        image = Image.new("1", (width, height), 1)
        for y in range(0, height, tile_size):
            for x in range(0, width, tile_size):
                values = decode(dictionary[indices[cursor]], tile_size, tile_size, 0)
                tile = Image.new("1", (tile_size, tile_size))
                tile.putdata([0 if bit else 255 for bit in values])
                image.paste(tile, (x, y))
                cursor += 1
        result.append(image)
    if cursor != len(indices):
        raise ValueError("Unexpected tile indices")
    return result
