# SSD2683 calibration tools

Maintained, offline waveform catalog and exporter. All earlier fast-BW,
reset/unit-weight, weak-white, screening and comparison candidates are preserved
with their original safety assertions and measurement notes. They are not linked
into firmware. A catalog entry is **not** a panel-qualified refresh sequence.

```sh
bash tools/display-calibration/run.sh --list
bash tools/display-calibration/run.sh --dump vendor-gray16.0
```

Dump emits JSON containing the complete 535-byte payload. It performs no USB,
GPIO, SPI or flash writes. Power/VCOM/PLL/border setup and refresh history must
also be considered before using any payload. Do not upload arbitrary LUT bytes
to production firmware; its calibration API only permits whole vendor records
with fixed analog settings, code-0 hold and white preclear.

The storage test compares all five production passes and packed pixel codes
against this catalog. Gray recipes/reflectance and spatial-debt/energy parameters
are separately persisted and updated through USB local confirmation. See
[EPD API](../../docs/EPD_API.md#calibration) and
[display architecture](../../docs/DISPLAY_ARCHITECTURE.md).
