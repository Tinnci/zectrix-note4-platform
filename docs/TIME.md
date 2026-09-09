# Persistent clock and RTC ownership

S1.3 makes `TimeService` the foreground owner of wall time, UTC conversion and
RTC calibration. Platform initializes Storage before Time, and restores the
clock before starting Connectivity or the application runtime. Clock, the
status bar and sleep covers all read `TimeService::Now()`; they do not perform
their own I2C reads. This adds no task, periodic wake or display refresh during
sleep.

## Setting the clock

Open **CLOCK**, then press OK to enter **SET CLOCK**. UP increases the selected
field, DOWN decreases it and OK advances through year, month, day, hour,
minute and UTC offset. The offset changes in 15-minute steps within ±14 hours.
OK on **SAVE DATE AND TIME** applies the draft with seconds set to zero. At the
Save row, UP returns to the offset and DOWN returns to the year. Hold OK to
cancel the draft; hold DOWN remains global shutdown. February and month lengths
are clamped when changing the year or month. An unset clock starts an editable
2000-01-01 draft; that date is never applied without Save.

An enrolled Android Companion also supplies its current time and UTC offset
on Hello/reconnect. The offset includes the phone's current daylight-saving
adjustment. Offline operation retains that fixed offset; reconnect or edit it
after a timezone or daylight-saving change.

| Clock label | Meaning |
| --- | --- |
| `RTC SAVED` | The RTC and stored UTC offset agree, or boot restoration succeeded |
| `SYSTEM TIME - SAVE PENDING` | Time is valid for this boot; RTC persistence failed and will retry |
| `LOCAL TIME - SET UTC OFFSET` | A retained RTC calendar is valid, but its UTC offset is unknown |
| `SYSTEM UTC - SET CLOCK` | System time exists without a configured local offset |
| `TIME NOT SET` / `UPTIME` | No valid calendar exists; the display explicitly shows elapsed time |

## Boot and sampling

The RTC retains local calendar fields, preserving the existing format and
Storage key `rtc_utc_offset` (signed seconds east of UTC). Boot reads control
and calendar registers together, rejects voltage-low, STOP, TEST1/TESTC,
unsupported century, malformed BCD and invalid Gregorian dates, then converts
a valid local date to UTC with the saved offset. It never changes the process
timezone and never derives real time from the firmware build timestamp.

A valid legacy RTC with no valid offset is displayed from a monotonic anchor.
It does not initialize the UTC clock used for TLS. Missing or invalid RTC data
does not fail Platform startup or Clock entry. `Now()` reads the system clock,
or the local RTC anchor, or explicit uptime without I2C/NVS work. Failed boot
restoration and pending persistence retry at most once per minute through
`Platform::Poll()` on the existing foreground loop. A successfully restored
clock does not trigger repeated RTC reads or writes while awake. Certificate
date checks remain enabled.

## Calibration and interrupted writes

`SetLocalTime()` and `SetUnixTime()` validate the supported local calendar
(2000–2099) and offset before changing anything. `ESP_OK` means the system
clock was set for this boot. `Status().rtc_persisted`, `persistence_pending`
and `last_error` describe the separate durable save. Invalid input or a failed
system-clock call leaves the previous calibration unchanged.

Calibration persists in this order:

1. Set PCF8563 STOP, with reserved and test bits cleared.
2. Commit the UTC offset to NVS if it changed or was previously unknown.
3. Sample current system time after the NVS operation and write all seven RTC
   calendar registers in one I2C transaction, clearing the voltage-low flag.
4. Clear STOP only after the complete calendar write succeeds.

RTC registers and NVS cannot share a transaction. For example, power loss
after saving a new offset but before writing its matching local calendar could
otherwise restore a plausible but wrong UTC time. The chip's native STOP bit
keeps such interrupted updates invalid on the next boot. No additional journal,
hash or persistent format is needed. A retry writes the current time, rather
than replaying an old sample. Unchanged offsets do not cause another NVS write.

An I2C/NVS failure leaves the system clock usable and the UI explicit about
the pending save. A failed calendar write deliberately stays stopped until
recalibration. Ordinary initialization and shutdown never write the calendar
or stop the RTC. Closing I2C handles does not stop the external oscillator.

## Companion handoff

Hello has an optional TLV type `5`: 12 bytes containing uint64 little-endian
Unix milliseconds followed by signed int32 little-endian UTC offset seconds.
Existing peers can omit or ignore it without changing protocol version 1.0.
Malformed optional hints are ignored; malformed required hints are rejected.
Duplicate hints are not applied.

The session owner queues a copied hint only after enrollment/reconnect peer
authorization, successful sync-cursor reconciliation and HelloAck submission.
The foreground owner consumes it only for the still authorized current
transport session. Disconnect, replacement, bond clearing or an age above
30 seconds drops it. A 64-bit monotonic receive timestamp accounts for queue
delay, including uptime beyond the 32-bit millisecond wrap. No RTC, system
clock or UI operation runs on a BLE callback/session task. The PCF8563 has
one-second calendar resolution; this handshake does not measure BLE transit
latency or promise NTP-grade precision.

## Hardware evidence and limits

The firmware uses PCF8563 at I2C address `0x51`, SDA/SCL GPIO47/48 and INT GPIO5.
The [NXP PCF8563 data sheet, revision 11.1](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf)
documents the control bits in section 8.3.1, voltage-low detection in 8.4.1.1,
single-access calendar transfers in 8.5, and STOP in 8.10. STOP resets divider
stages; it is used only for calibration. The firmware disables unused CLKOUT
at initialization. The specified typical **chip** current is 250 nA at 3 V
and 25 °C with CLKOUT and the I2C interface inactive (table 29).

PCF8563 has one VDD pin, not a separate VBAT pin. Retention therefore depends
on the board's external supply path keeping VDD above the data-integrity limit
while the ESP32's latch/rails are off. The [official hardware publication
policy](https://wiki.zectrix.com/zh/software/opensource) makes schematics and
batch details available by request; those files are not present in this
repository. Software GPIO names do not establish the RTC backup topology.
The existing rail-off GPIO holds and shutdown timing remain intact. Battery
off/deep-sleep retention, backup voltage and whole-board standby current still
require measurements on the actual revision; the chip specification is not a
measurement of this device.

## Verification

`bash tools/test-host.sh` passes all 32 targets. Time tests intercept the host
clock call and cover cold restart, leap days, offset/day boundaries, missing
offset/RTC, staged failures and delayed retries. Board tests exercise the real
I2C/RTC driver, torn writes, STOP/test/century/VL rejection, and unchanged
calendar/control registers across normal shutdown and reinitialization.
Platform tests verify Storage-before-Time-before-Connectivity and nonfatal
RTC restoration across five service compositions. Catalog/runtime tests use
one descriptor for the menu label and navigation target; controller tests
cover empty/reduced menus and bounded clock edits. Companion tests cover wire
encoding, unauthorized/replaced/expired handoff and millisecond wrap.

The 29 Android JVM tests and debug APK build pass, as do ESP32-S3 builds for
Full, Core, Offline Reader, BLE and Web Transfer. See [MODULAR_BUILD.md](MODULAR_BUILD.md)
for profile configuration and [HARDWARE.md](HARDWARE.md) for shutdown wiring.
The connected ESP32-S3 flash/boot smoke also passes, including PSRAM, partitions
and the foreground runtime. Its retained RTC was invalid and startup continued
through the explicit fallback. Physical phone calibration and power-off RTC
retention remain separate hardware checks.
