# Third-party notices

This project is distributed under the MIT License. The following third-party
software or assets retain their own licenses.

## TRMNL16 Regular

The embedded ASCII bitmap table in
`components/zectrix_demo_ui/font/zectrix_ascii_font_8x16.h` was rasterized from
TRMNL16 Regular by Heavyweight Digital Type Foundry. The font is licensed under
the SIL Open Font License, Version 1.1, included at
`licenses/TRMNL_FONT_LICENSE.txt`.

## Reader CJK bitmap font

`components/zectrix_reader/font/reader_font.bin` is a subset of GNU Unifont
15.1.05, distributed under SIL Open Font License 1.1. The full copyright notice,
source and regeneration instructions are in
[the font README](components/zectrix_reader/font/README.md); the license is in
[OFL-1.1.txt](components/zectrix_reader/font/OFL-1.1.txt).

## miniz inflate

The reader includes the miniz `tinfl` decompressor under its
[MIT license](components/zectrix_reader/third_party/miniz/LICENSE).
Copyright 2013-2014 RAD Game Tools and Valve Software; copyright 2010-2014
Rich Geldreich and Tenacious Software LLC. ZIP and EPUB handling are implemented
in this repository.

## Espressif ESP-IDF

ESP-IDF is provided by Espressif Systems under its respective open-source
licenses. It is a build dependency and is not copied into this project.

## Espressif esp_codec_dev

The audio codec abstraction is obtained through ESP-IDF Component Manager as
`espressif/esp_codec_dev`. Its license file is delivered with the downloaded
component in `managed_components/espressif__esp_codec_dev/LICENSE`.

## Demonstration artwork

The lighthouse, snowy-path and mountain images in `main/assets` were created
for the Zectrix hardware demonstration and are distributed with this project
under the project MIT License by Zectrix Lab.
