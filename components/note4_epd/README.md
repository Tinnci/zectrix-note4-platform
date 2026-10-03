# note4_epd

An SSD2683 driver for the Note4 4.2-inch e-paper display, qualified in this
platform with ESP-IDF 5.5.2. The manifest's 5.4+ constraint is not a claim that
every accepted IDF version has been tested.
The component has a single public C header, `include/note4_epd.h`, and does
not depend on LVGL.

Copy this directory to `components/note4_epd` in a third-party project. The
default pin configuration targets the Note4 ESP32-S3 e-paper board and can
be overridden through `note4_epd_config_t`.

The API provides explicit panel power-on/power-off, full 1bpp refresh, partial
1bpp refresh and full 4bpp/16-gray refresh. See the
[EPD API](../../docs/EPD_API.md) for lifecycle, formats and integration examples.
Platform applications use [DisplayService](../../docs/DISPLAY_ARCHITECTURE.md)
rather than calling this raw driver directly.

Copyright (c) 2026 Zectrix Lab. Licensed under MIT.
