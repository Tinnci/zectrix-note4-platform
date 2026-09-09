#!/usr/bin/env python3
"""Generate small EPUB inputs with a ZIP implementation independent of the reader."""

import io
import sys
import zipfile
from pathlib import Path


def epub(path, compression, chapters=None):
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
        book.writestr("OPS/package.opf", '<package><manifest>' + manifest + '</manifest><spine>' + spine +
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
