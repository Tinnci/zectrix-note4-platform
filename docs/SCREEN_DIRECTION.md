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

The rotated modes allocate a reusable 60,000-byte scratch buffer on first use;
standard mode allocates none.

## Application portrait (90° / 270°) · 应用竖屏

**DISPLAY / 屏幕** now cycles 0° → 90° → 180° → 270°. `ui.orientation` stores
0 (0°), 1 (180°), 2 (90°) and 3 (270°); older 0/1 values keep their meaning.

Portrait-capable screens draw on a native 300×400 canvas (packed scanlines, same
15,000 bytes) and are presented through `PresentPortrait1Bpp(intent, …)`. The
first portrait frame after any landscape frame or direction change is a clean full
refresh; later portrait frames use the normal compare path, so a focus move can
still refresh partially. Buttons keep their semantic meaning in every direction.

| Screen | Portrait layout |
| --- | --- |
| Home | Date strip and reading card stacked on top, then one column of six tiles (same focus order and paging as landscape) |
| Tools / Settings / menus | Full-width rows; eleven-entry menus fit without scrolling |
| Reader | 284×308 text body (re-paginated; bookmarks are source offsets, so position survives rotation), library with nine rows, options |
| Send Books | Wrapped 268 px text column: mode choice, hotspot/Wi-Fi session, result and errors |
| Pocket Tools, Clock, Sleep Cover menu | Native portrait; the month grid uses a 40 px column pitch |

The status bar anchors its right-hand icons to the right edge (shifted 100 px
left). Footers that do not fit one line split at the gap between hints.
**Landscape-only screens** — Lua apps, USB Manager, Connectivity, Gallery,
hardware tests, Device Info and About — still use a 400×300 canvas. They are shown
unrotated (90°) or turned 180° (270°) and the next portrait screen restores the
portrait canvas with a full refresh. Reader orientation applies when a book opens.

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
