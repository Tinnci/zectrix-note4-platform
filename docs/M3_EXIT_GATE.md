# M3 static-application exit gate

Status: Hardware qualification required. This gate does not freeze the source
API or ABI.

## Automated evidence

- Launcher, Clock, Settings, and Diagnostics use one immutable registry and
  one `ApplicationRuntime` lifecycle boundary.
- Controllers define product decisions. They do not expose FreeRTOS objects.
- The architecture checker rejects board support, ESP-IDF drivers, FreeRTOS
  headers, task handles, queue handles, and semaphore handles in application
  code.
- Runtime tests cover deferred navigation, one pending render, dirty-region
  union, `Quality` priority, stale-generation discard, render failure, and
  exactly-once foreground destruction.
- Display tests cover eight partial refreshes, full-refresh promotion, 4bpp
  baseline invalidation, refresh-error invalidation, and 1bpp recovery.
- Power tests cover wake-reason mapping and the qualified shutdown order.

## Heap comparison

Firmware logs two snapshots during boot:

1. `M2-equivalent platform` after Platform creates all M2 services.
2. `M3 runtime active` after the first foreground application enters.

Record `free`, `min`, and `largest` values from one boot. The difference in
free internal heap is the M3 foreground/runtime cost relative to the same
boot's M2-equivalent service baseline. Use the minimum-free value after the
navigation soak as the peak-use evidence.

## Hardware gate

Run these checks on the same candidate firmware:

- switch repeatedly among Launcher, Clock, Settings, and Diagnostics;
- confirm Clock changes after the RTC minute changes;
- save Settings, reboot, and confirm persistence;
- run all Diagnostics and one individual test;
- make at least eight menu selection updates and confirm the promoted full
  refresh has no abnormal ghosting;
- enter the 4bpp Gallery, return to a 1bpp application, and then request a
  partial update;
- confirm the Launcher idle timeout with Auto Showcase on and off;
- execute shutdown, wake the device, and confirm a fresh Launcher lifecycle;
- confirm no panic, watchdog, unexpected reset, ownership bypass, or second
  foreground application instance.

Record the heap log values, build size, commit, and hardware result before
issues #22 and #23 and milestone M3 close.
