# Zectrix board hardware map

The standalone board adapter is in `components/zectrix_board`. The defaults
target the current Zectrix 4.2-inch ESP32-S3 e-paper board.

## GPIO map

| Function | GPIO / address | Notes |
| --- | --- | --- |
| OK button | GPIO0 | Active low |
| UP button | GPIO39 | Active low |
| DOWN / power button | GPIO18 | Active low. Hold for 3 seconds to shut down; release and press to wake. EXT1 ANY_LOW during USB-powered deep sleep. |
| Battery power latch | GPIO17 | High keeps battery rail on |
| Power LED | GPIO3 | Active low |
| Audio rail | GPIO42 | Active high |
| Audio MCLK / BCLK / WS | GPIO14 / 15 / 38 | ES8311 |
| Audio data out / in | GPIO45 / 16 | ESP32 perspective |
| Speaker PA | GPIO46 | Amplifier enable |
| I2C SDA / SCL | GPIO47 / 48 | RTC, NFC and audio control |
| RTC interrupt | GPIO5 | PCF8563 at address `0x51` |
| NFC power / field detect | GPIO21 / GPIO7 | NFC at address `0x55` |
| Charge detect / full | GPIO2 / GPIO1 | Charger status inputs |

The EPD GPIO and SPI defaults are public through
`zectrix_epd_get_default_config()`. Applications can override each value in the
returned `zectrix_epd_config_t` before creating the driver.

## Power behavior

At boot, the demo asserts the battery latch before peripheral initialization.
The display has a separate controlled rail and remains off until a refresh.
The board initializes audio when the audio test first runs.

Shutdown presents the selected [sleep cover](SLEEP_COVER.md), then releases
service and board peripherals before cutting the audio rail
and battery latch. I2C, I2S and owned EPD signal pins are disconnected, and
ESP32-S3 deep-sleep GPIO holds retain rail-off levels while USB supplies power.
Before latch release, board support waits up to about five seconds for three
released DOWN samples at 20 ms intervals. It configures RTC-capable GPIO18 as
input with a pull-up, disables its pull-down and arms EXT1 ANY_LOW without
forcing the RTC peripheral power domain on. Boot releases RTC hold/mode before
normal button initialization. If release or wake setup fails, shutdown still
cuts the rails; USB sleep then requires reset or power cycling. No timer wake
is configured.

Host tests verify the driver calls and cleanup order. Current consumption and
physical BLE/Wi-Fi coexistence still require board measurement with both
battery and USB power configurations.

The external PCF8563 is restored by TimeService before networking starts.
Initialization disables unused CLKOUT without resetting the calendar, and
ordinary shutdown does not stop its clock. Its single VDD supply must remain
powered by the board's backup path for retention after latch release. The
available GPIO map does not establish that path or its retention duration;
see [TIME.md](TIME.md) for datasheet evidence and measurement limits.

Battery voltage is read through the board ADC path and displayed as both
millivolts and an estimated percentage. The charging test combines charger
status pins with the battery measurement to reject a false pass when no
battery is fitted.

## Porting to another revision

1. Update `components/zectrix_board/include/zectrix_board_config.h`.
2. Update `zectrix_epd_get_default_config()` in `components/zectrix_epd/zectrix_epd.cc`
   if the EPD SPI wiring changed.
3. Confirm flash size, PSRAM mode and partition layout in `sdkconfig.defaults`.
4. Re-run every item in `docs/TEST_CRITERIA.md` on real hardware.
