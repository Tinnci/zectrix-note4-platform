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
layout and reader pagination design and are not included in these two modes.

Host display tests cover asymmetric pixel placement, packed patch rotation,
direction changes, unchanged-frame suppression and grayscale submissions.
Physical panel rotation and button ergonomics still require device testing.
