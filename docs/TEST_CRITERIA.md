# Hardware test criteria

The test implementation is in `components/zectrix_self_test`. Interactive
tests time out after 60 seconds unless stated otherwise.

| Test | PASS criterion | Operator setup |
| --- | --- | --- |
| Wi-Fi RF | Generic mode: at least one 2.4 GHz AP found. Qualification mode: configured SSID observed three consecutive times at or above the RSSI threshold. | Provide a 2.4 GHz AP. Configure SSID and threshold for production use. |
| Audio | Generated AFSK sequence is played, recorded by the microphone and decoded successfully. | Use a reasonably quiet space. Do not cover the speaker or microphone. |
| RTC | PCF8563 one-second countdown asserts its status flag or interrupt within the retry window. | No operator action. |
| Power | Charging is active, or the charger reports full while a valid battery measurement exceeds 97%. No-battery and fault states fail. | Connect a battery and USB power. |
| LED | Operator confirms that the power LED visibly blinks. | Press OK for PASS. Press DOWN for FAIL. |
| Buttons | Correct sequence `OK`, `UP`, `DOWN` is received. UP/DOWN trigger on press. OK triggers on release. Wrong input restarts the sequence. | Do the on-screen steps. |
| NFC | User memory is backed up, a temporary URL NDEF message is written and read back, a phone field is detected, then the original bytes are restored. | Hold an NFC-capable phone near the antenna when prompted. |

## Controls during tests

- Hold OK for 1.5 seconds to cancel the current test and return.
- Hold DOWN for 3 seconds to present the selected sleep cover and shut down.
- Run All records PASS/FAIL for each test and ends on a seven-item summary.

## Sleep cover and wake check

1. Read a page, return home and open **SLEEP COVER**. Select the dashboard and
   check its month grid, last saved title and progress. An unset clock must show
   **TIME NOT SET**. Check landscape and blank previews, then return with hold OK.
2. Use OK in Preview or hold DOWN for three seconds to sleep. Release DOWN.
   Confirm that the selected surface remains, with no live radio/status overlay;
   Blank must be entirely white. Check shutdown from a gray gallery page too.
3. With USB power, press DOWN again. Confirm a fresh boot, working buttons and
   the saved cover choice. On battery, confirm latch shutdown and power-button
   startup separately. Check that reader progress resumes correctly.
4. Keep DOWN held beyond the roughly five-second release wait. Confirm shutdown
   still completes without repeated restart. USB recovery in this case requires
   reset or power cycling because button wake was not armed.
5. Measure settled standby current in battery and USB configurations with the
   appropriate fixture. Record the supply, peripherals and measured current;
   boot smoke and Host tests do not establish microamp consumption.

These are manual hardware checks. The L1.4 boot smoke test verifies flashing
and startup only. Implementation and Host coverage are in
[SLEEP_COVER.md](SLEEP_COVER.md).

## Production recommendations

Generic Wi-Fi scan is intended for demonstrations only. Set
`CONFIG_ZECTRIX_DEMO_RF_TARGET_SSID` and choose an RSSI threshold that matches
the fixture before you use RF results for qualification.

The LED test is intentionally visual. For a fully automated fixture, add an
optical sensor and replace the operator result with a measured threshold.

The NFC test is designed to preserve existing user data. Power must remain
stable through the restore step. A production fixture must also verify the
restored bytes after an interrupted-test recovery procedure.
