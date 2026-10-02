# Screen direction

Settings includes **DISPLAY / 屏幕**, toggled with OK between standard (0°)
and inverted (180°). The preference is stored by StorageService as
`ui.orientation` (0 or 1) and restored before the splash screen. A failed save
restores the previous direction; invalid stored values use the standard mode.

DisplayService rotates packed 1-bit and 4-bit frames at the panel boundary.
Application layouts retain their 400×300 coordinates. Partial refresh pixels
and windows rotate together, and changing direction requires a full refresh.
Caller-owned image buffers remain unchanged. All applications and sleep covers
use the selected direction. Buttons retain their existing semantic actions.

The inverted mode allocates a reusable 60,000-byte scratch buffer on first use;
normal mode allocates none. Portrait 90°/270° layouts require a separate 300×400
layout and reader pagination design and are not included in these application modes.

## Portrait calendar lock screen

**SLEEP COVER / 休眠封面** selects 0° or 90° via `ui.sleep_dir`.
Missing or invalid settings default to 90°; valid choices are preserved.
Only the calendar and its preview rotate. Apps, reader pagination and picture
covers are unchanged. The portrait calendar renders at native 300×400 with a
full clean refresh. Leaving preview restores the app's direction and refreshes.

Both calendar layouts use Monday-first columns and a solid marker for today.
The portrait header includes battery level (or `--%` when unknown); the six-week
grid leaves separate space for weather/daily text, update time and wake controls.
Landscape lists reserve solid fill for selection, keeping inactive rows unboxed.
Reader pagination, app orientation and refresh policy are unchanged.

## Daily calendar refresh

With a valid clock, the calendar schedules deep sleep until local 00:01.
This retains battery power, rather than cutting the rail. DOWN opens the normal
UI; timer wake skips the splash and launcher, refreshes the calendar and sleeps.
Scheduling reads the clock after display I/O to avoid accumulated delays.
The existing timezone applies; automatic DST is not added.

Blank, quote and picture covers retain rail-off behavior. Invalid clock,
battery ≤5% without external power, or failed button/timer setup also falls back
to shutdown. OTA trials still require normal Home confirmation; unattended
wakes cannot confirm them. Calendar refresh alone adds no daily flash writes
or radio sessions; optional remote refresh is separate.

Hardware tests remain: standby current, timer drift, date rollover, button wake
and OTA trials. Host tests do not replace these measurements.

Host display tests cover asymmetric pixel placement, packed patch rotation,
direction changes, unchanged-frame suppression and grayscale submissions.
Physical panel rotation and button ergonomics still require device testing.
