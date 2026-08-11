# Quick start

## 1. Install ESP-IDF

Install the project-qualified ESP-IDF v5.5.2 baseline and open an ESP-IDF-
enabled shell. Confirm that the tools are available:

```bash
idf.py --version
```

## 2. Configure and build

From the standalone demo directory:

```bash
idf.py set-target esp32s3
idf.py build
```

The supplied defaults select 16 MB flash, octal PSRAM and a 3 MB factory app
partition. `set-target` creates a local `sdkconfig`. Delete that generated file
before you change targets or apply revised defaults to a configured copy.

## 3. Flash and monitor

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

On Linux, the device can appear as `/dev/ttyACM0` or `/dev/ttyUSB0`. On macOS,
look for `/dev/cu.usbmodem*`. On Windows, use the corresponding `COM` port.

Exit the monitor with `Ctrl+]`.

## 4. First run

After the splash screen, use UP/DOWN to move and OK to select. If no key is
pressed for 15 seconds, Auto Showcase begins. Hold OK for 1.5 seconds to
return. Hold DOWN for 3 seconds from any normal screen to clear the e-paper and
shut down.

For a full hardware check, open **Hardware Tests**, select **Run All Tests**,
and do the on-screen steps. Have the following ready:

- a visible 2.4 GHz Wi-Fi access point.
- a reasonably quiet environment for the acoustic loopback.
- USB power and a connected battery for the charging test.
- an NFC-capable phone for field detection.

## Configuration

```bash
idf.py menuconfig
```

Use **Zectrix hardware showcase** to set a qualification SSID, RSSI threshold
and temporary NFC URL. Rebuild and reflash after changing configuration.

## Common problems

### The component download fails

Confirm network access to the ESP Component Registry, then retry `idf.py build`.
The project declares the component and version constraint in
`main/idf_component.yml`.

### The board repeatedly resets after flashing

Confirm that the target is `esp32s3`. Confirm that the board has 16 MB flash
and octal PSRAM. Flash the whole project, not only the app image.

### BUSY timeout during an e-paper refresh

Confirm the board revision and display cable, then verify the EPD pin mapping
in `components/zectrix_epd/zectrix_epd.cc`. Do not start a partial refresh
before a successful full 1bpp base refresh.

### The board does not power off while connected to USB

This is expected. USB keeps the rail powered. The demo clears the panel and
enters deep sleep. Disconnect USB to validate battery-latch shutdown.
