# Third-party notices

This project is distributed under the MIT License. The following third-party
software or assets retain their own licenses.

## TRMNL16 Regular

The embedded ASCII bitmap table in
`components/ui/font/ascii_font_8x16.h` was rasterized from
TRMNL16 Regular by Heavyweight Digital Type Foundry. The font is licensed under
the SIL Open Font License, Version 1.1, included at
`licenses/TRMNL_FONT_LICENSE.txt`.

## Reader CJK bitmap font

`components/note4_reader/font/reader_font.bin` is a subset of GNU Unifont
15.1.05, distributed under SIL Open Font License 1.1. The full copyright notice,
source and regeneration instructions are in
[the font README](components/note4_reader/font/README.md); the license is in
[OFL-1.1.txt](components/note4_reader/font/OFL-1.1.txt).

## miniz inflate

The reader includes the miniz `tinfl` decompressor under its
[MIT license](components/note4_reader/third_party/miniz/LICENSE).
Copyright 2013-2014 RAD Game Tools and Valve Software; copyright 2010-2014
Rich Geldreich and Tenacious Software LLC. ZIP and EPUB handling are implemented
in this repository.

## Lua

The optional micro-app runtime builds the core and auxiliary API of
[Lua 5.4.9](https://github.com/lua/lua/tree/v5.4.9). Lua is fetched at build time;
its standard libraries are not opened for guests. Copyright (C) 1994–2026
Lua.org, PUC-Rio. Its MIT license is included in [licenses/LUA_LICENSE.txt](licenses/LUA_LICENSE.txt).

## Optional Wasm interpreters

WAMR 2.4.5 is fetched from [WebAssembly Micro Runtime](https://github.com/wasm-micro-runtime/wasm-micro-runtime/tree/WAMR-2.4.5)
under Apache License 2.0 with LLVM exceptions; upstream `LICENSE` and copyright
notices remain in the build copy and [WAMR_LICENSE.txt](licenses/WAMR_LICENSE.txt).
Wasm3 0.5.0 is fetched from
[Wasm3](https://github.com/wasm3/wasm3/tree/v0.5.0) under MIT; its `LICENSE`
remains in the build copy and [WASM3_LICENSE.txt](licenses/WASM3_LICENSE.txt).
Maintained patches are identified in
`components/note4_runtime/patches` and modify only build-owned copies.

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
