# note4_epd

An SSD2683 driver for the Note4 4.2-inch e-paper display, qualified in this
platform with ESP-IDF 5.5.2; builds also support 6.0.3. The manifest's 5.4+ constraint is not a claim that
every accepted IDF version has been tested.
The public C APIs are `include/note4_epd.h` (hardware) and
`include/note4_epd_calibration.h` (hardware-independent profile/codec).
The component depends on neither LVGL nor NVS.

Copy this directory and `note4_log` into a third-party project's `components`. The
default pin configuration targets the Note4 ESP32-S3 e-paper board and can
be overridden through `note4_epd_config_t`.

The API provides explicit panel power-on/power-off, full 1bpp refresh, partial
1bpp refresh, native 2bpp input and full 4bpp/16-code gray refresh. Optical separation is
panel-specific, not a promise of sixteen equally spaced gray states. See the
[EPD API](../../docs/EPD_API.md) for lifecycle, formats and integration examples.
Platform applications use [DisplayService](../../docs/DISPLAY_ARCHITECTURE.md)
rather than calling this raw driver directly.

Gray rendering is a production module: fixed vendor analog settings, an OTP
white base, five ordered passes, and bounded whole-record recipe selection.
Calibration can be replaced with the rail off. The platform independently
persists a 92-byte profile and restores it before the first frame; USB updates
apply on reboot. The offline catalog stays in `tools/display-calibration`, outside the
firmware build. See [calibration](../../docs/EPD_API.md#calibration).

Copyright (c) 2026 Zectrix Lab. Licensed under MIT.
