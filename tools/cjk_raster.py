"""Shared offline FreeType rasterization; no firmware dependency."""
import freetype as ft
import numpy as np
from PIL import Image


def render(face, char, width, height, baseline, scale, flags, mono=False, phase=(0., 0.), reject_clipped=False, include_outline=False):
    face.set_pixel_sizes(0, scale * face.ui_size)
    # FreeType's y axis is upward; phases are expressed in native canvas pixels.
    identity = ft.Matrix(0x10000, 0, 0, 0x10000)
    face.set_transform(identity, ft.Vector(round(phase[0]*scale*64), round(-phase[1]*scale*64)))
    try:
        face.load_char(char, flags | ft.FT_LOAD_RENDER)
    finally:
        # A candidate must not leave a transform on the shared face/reference.
        face.set_transform(identity, ft.Vector(0, 0))
    bitmap = face.glyph.bitmap
    result = np.zeros((height * scale, width * scale), dtype=np.uint8)
    if bitmap.rows and bitmap.width:
        pitch = abs(bitmap.pitch)
        # freetype-py 2.5.1's public buffer property builds a Python list one byte
        # at a time. View its retained C FT_Bitmap instead; result copies the rows
        # before another load_char can invalidate this view. Same pixels/pitch.
        raw = np.ctypeslib.as_array(bitmap._FT_Bitmap.buffer,
                                   shape=(bitmap.rows*pitch,)).reshape(bitmap.rows, pitch)
        if bitmap.pitch < 0:
            raw = raw[::-1]
        pixels = (np.unpackbits(raw, axis=1)[:, :bitmap.width] * 255 if mono
                  else raw[:, :bitmap.width])
        x, y = face.glyph.bitmap_left, baseline * scale - face.glyph.bitmap_top
        left, top = max(0, x), max(0, y)
        right, bottom = min(result.shape[1], x + bitmap.width), min(result.shape[0], y + bitmap.rows)
        if right > left and bottom > top:
            result[top:bottom, left:right] = pixels[top-y:bottom-y, left-x:right-x]
        if reject_clipped and int(result.sum(dtype=np.uint64)) != int(pixels.sum(dtype=np.uint64)):
            raise ValueError("Subpixel candidate clips outline coverage at the unchanged canvas boundary")
    native = np.asarray(Image.fromarray(result).resize((width, height), Image.Resampling.BOX), dtype=float) / 255
    return (native, result.astype(float)/255) if include_outline else native
