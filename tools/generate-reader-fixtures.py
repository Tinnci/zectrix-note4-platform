#!/usr/bin/env python3
"""Generate small EPUB inputs with a ZIP implementation independent of the reader."""

import io
import struct
import sys
import zipfile
from pathlib import Path


def epub(path, compression, chapters=None, metadata_padding=0):
    content = chapters or [
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>HIDDEN TITLE</title>'
        '<style>HIDDEN STYLE</style></head><body><!-- hidden comment -->'
        '<p>第一章。你好，世界！English words stay together &amp; entities &#x4E2D;&#25991;.</p>'
        '<p>' + '风从海上来。The quiet reader turns another page. ' * 220 + '</p>'
        '<script>HIDDEN SCRIPT</script></body></html>',
        '<html><body><h1>SECOND CHAPTER</h1><p>日本語と한국어。最后一页。</p></body></html>',
    ]
    with zipfile.ZipFile(path, "w", compression=compression) as book:
        book.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        book.writestr("META-INF/container.xml", '<?xml version="1.0"?>'
                      '<container><rootfiles><rootfile media-type="application/oebps-package+xml" '
                      'full-path="OPS/package.opf"/></rootfiles></container>')
        manifest = ''.join(f'<item href="text/part%20{i}.xhtml" media-type="application/xhtml+xml" id="c{i}"/>'
                           for i in reversed(range(len(content))))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(len(content)))
        book.writestr("OPS/package.opf", '<package><metadata><!--' + 'x' * metadata_padding +
                      '--></metadata><manifest>' + manifest + '</manifest><spine>' + spine +
                      '<itemref linear="no" idref="unused"/></spine></package>')
        for index, text in enumerate(content):
            book.writestr(f"OPS/text/part {index}.xhtml", text)


def main():
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    epub(output / "stored.epub", zipfile.ZIP_STORED)
    epub(output / "deflated.epub", zipfile.ZIP_DEFLATED)
    epub(output / "long-hidden.epub", zipfile.ZIP_DEFLATED,
         ['<html><head><style>' + 'x' * 200000 + '</style></head><body><p>VISIBLE</p></body></html>'])
    epub(output / "empty-chapter.epub", zipfile.ZIP_DEFLATED,
         ['<html><body></body></html>', '<html><body><p>Visible chapter</p></body></html>'])
    epub(output / "long-metadata.epub", zipfile.ZIP_DEFLATED, metadata_padding=200000)
    epub(output / "huge-metadata.epub", zipfile.ZIP_DEFLATED, metadata_padding=270000)
    epub(output / "bzip2.epub", zipfile.ZIP_BZIP2)
    epub(output / "encrypted.epub", zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(output / "encrypted.epub", "a") as encrypted:
        encrypted.writestr("META-INF/encryption.xml", "<encryption/>")
    corrupted = bytearray((output / "stored.epub").read_bytes())
    index = corrupted.index(b"English words")
    corrupted[index] = ord("O")
    (output / "bad-crc.epub").write_bytes(corrupted)
    epub(output / "empty-blocks.epub", zipfile.ZIP_DEFLATED,
         ['<html><body><p>VISIBLE</p></body></html>'])
    raw = bytearray((output / "empty-blocks.epub").read_bytes())
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        chapter = archive.getinfo("OPS/text/part 0.xhtml")
        directory = archive.start_dir
    data_start = chapter.header_offset + 30 + len(chapter.filename.encode())
    # Legal non-final empty DEFLATE blocks must yield without producing text.
    padding = b'\x00\x00\x00\xff\xff' * 4096
    raw[data_start:data_start] = padding
    struct.pack_into('<I', raw, chapter.header_offset + 18, chapter.compress_size + len(padding))
    cursor = directory + len(padding)
    while raw[cursor:cursor + 4] == b'PK\x01\x02':
        name_size, extra_size, comment_size = struct.unpack_from('<HHH', raw, cursor + 28)
        if struct.unpack_from('<I', raw, cursor + 42)[0] == chapter.header_offset:
            struct.pack_into('<I', raw, cursor + 20, chapter.compress_size + len(padding))
        cursor += 46 + name_size + extra_size + comment_size
    struct.pack_into('<I', raw, cursor + 16, directory + len(padding))
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        assert b'VISIBLE' in archive.read(chapter.filename)
    (output / "empty-blocks.epub").write_bytes(raw)
    # An unseekable writer emits bit-3 data descriptors and zero local sizes.
    class Sequential(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation("sequential")
    stream = Sequential()
    epub(stream, zipfile.ZIP_DEFLATED)
    (output / "descriptor.epub").write_bytes(stream.getvalue())


if __name__ == "__main__":
    main()
