# M2 time behavior baseline

Status: Draft.

This document records the qualified time behavior before the M2.4 ownership
migration. The migration changes the dependency boundary. It does not change
the time behavior.

## RTC behavior

- Board initialization probes the RTC on I2C.
- The board reports that the RTC is unavailable when initialization or the
  probe fails.
- RTC calendar values use local calendar fields.
- A read fails when the RTC voltage-low flag is set.
- The current RTC implementation reads and writes years from 2000 through
  2099.
- `TimeService` rejects invalid calendar fields before it starts an I2C write.
- The RTC driver writes the seven calendar registers in one I2C transaction.
- The RTC countdown test requests one second.
- Each test attempt waits for a maximum of two seconds.
- Either the interrupt GPIO or the RTC timer flag can complete the test.
- The test stops the countdown and clears the timer flag after each attempt.
- The test makes a maximum of three attempts.

## Monotonic time behavior

- Elapsed-time measurements use the ESP-IDF monotonic microsecond clock.
- The migration does not change display-scene timing or self-test deadlines.
- The migration does not add timezone conversion or RTC-to-system-clock
  synchronization.

## Ownership after migration

- Application code uses `TimeService` for monotonic time and RTC status.
- Self-test code uses `TimeService` for deadlines and RTC countdown status.
- Board support owns the RTC implementation, I2C access, and interrupt GPIO.
- `TimeService` does not expose the PCF8563 type or its register interface.
