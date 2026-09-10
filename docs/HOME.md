# Home dashboard

E1.2 replaces the flat Launcher menu with a daily overview and application
tiles. It uses the existing 400 x 300 monochrome canvas, persistent status bar
and application runtime.

E1.7 adds Chinese and English labels through catalog presentation metadata.
Full starts in Chinese unless a saved preference selects English; Settings
contains the language picker. Focus, IDs and module composition remain stable
when language changes. See [LOCALIZATION.md](LOCALIZATION.md).

## Everyday controls

With Reader enabled, the first focus is the reading overview. OK resumes the
latest committed local position, including its font size. Without a bookmark,
the card says **OPEN LIBRARY** and opens the library. **BOOK READER** remains a
separate tile that always opens the library for choosing another book.

UP/DOWN follows one circular order: overview, then tiles from left to right
and top to bottom. Full has six tiles: Book Reader, Send Books, Clock, Sleep
Cover, Settings and Tools. The focused card or tile uses white text on black.
OK opens the selection. Hold OK returns from Tools to Home; hold DOWN retains
global shutdown. Home and Tools selections survive application exits in RAM.
E1.4 returns from a tool directly to its previous Tools row. Another Back
returns to Home's Tools tile. Explicit Home and entry-failure fallback still
land on Home. See [NAVIGATION.md](NAVIGATION.md) for the shared controls.

Tools contains Connectivity, Auto Showcase, Display Gallery, Hardware Tests,
Device Info and About & License. It is a private Launcher scene, with the
existing eight-row list and overflow indicator. Auto Showcase still respects
the saved preference and fifteen-second Home idle timeout. Tools does not
start it on idle.

## Module selection and layout

The composition catalog supplies labels, stable application IDs, icons and
Home placement. Disabled or unavailable optional services have no destination.
Minimal displays Clock, Sleep Cover, Settings and Tools in two rows, with a
calendar/system overview. It excludes the reading focus and reader fonts.
Reader and Send Books remain independently selectable.

At most six tiles fit on each page. Additional catalog entries use a page
indicator; crossing a page requests Quality. Short pages expand their rows.
An empty catalog shows **NO APPS AVAILABLE** and preserves shutdown. Tile labels
and UTF-8 book names are shortened to fit their bounds.

E1.6 centers the neutral **HOME** title below the status bar; multi-page counts
remain at the right. Seven original 16 x 16 monochrome glyphs identify the
catalog icons, including a distinct wrench for Tools. Their bitmap rows occupy
224 bytes of constant data. Each icon is vertically centered beside its label
and uses the tile's foreground color, including white on a focused black tile.

The overview shows the local date and a copied bookmark name/progress. An
unset calendar says **DATE NOT SET** and points to Clock. Unavailable reading
history has its own message. Home reads bookmarks once on entry and never
opens, paginates or synchronizes a book during drawing. Minute updates belong
to the status viewport; Home invalidates its date only when the day or calendar
validity changes.

## Continue Reading and failures

Continue Reading submits the normal owned `Open(reader)` command. The shell
holds a one-shot launch mode only after command acceptance; the inactive Reader
candidate copies it and the factory consumes it even if allocation fails.
No application pointer or borrowed bookmark crosses the lifecycle transition.
SDK v1 commands and persistent bookmark formats are unchanged.

Reader loads local history, refreshes the library and selects the saved
filename independently of its old list index. It checks the listed size,
the opened source length and the saved chapter/offset. Missing books, removed
list entries and unusable positions leave the user in Library with a visible
reason. A changed file is not silently restarted by Continue Reading. Ordinary
library selection can open it from the beginning. The existing first-32-books
listing bound and same-length edition limitation still apply; see
[READER.md](READER.md).

Opening EPUB metadata and laying out pages remain cooperative and cancellable.
Library stays beneath Reading on the scene stack. A loading cancellation or
failed display does not advance the bookmark; a successful display commits it
through the existing persistence/sync path. Phone progress still requires its
separate explicit action.

## Ownership and reference designs

The implementation follows the [E1.1 study](FIRMWARE_UI_STUDY.md): CrossPoint's
recent-reading entry and bounded pagination, Biscuit's grouping, and Flipper
Zero's retained scene state and viewport invalidation. It is original Note4
code using existing assets and fonts.

E1.6 also references CrossPoint's [HomeActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/home/HomeActivity.cpp)
for bounded header composition and [GfxRenderer](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/lib/GfxRenderer/GfxRenderer.cpp)
for pixel-aligned icons beside text. The Note4 glyphs are original; the existing
clipped canvas draws them without an asset decoder or another buffer.

Home and Tools use deferred SceneManager transitions. One foreground owner
draws the existing canvas and coalesces status/content into one DisplayService
commit. Focus requests Fast; scene/page changes and failed-frame recovery
request Quality. The display service continues to own partial refresh budgets
and panel recovery. No extra framebuffer, rendering task, network session,
timer wake or persistent setting is added. Send Books and static Sleep Cover
retain their existing service and shutdown ownership.

## Verification

```bash
mkdir -p build-ui-e1-2
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-ui-e1-2" bash tools/test-display-service.sh
ZECTRIX_READER_SANITIZE=1 bash tools/test-reader.sh
bash tools/test-minimal-profile.sh
```

The Host scenarios cover module combinations, ordered navigation, Tools return,
selection restoration, empty/single/full catalogs, tile pagination, midnight
changes, status preservation and failed-frame recovery. Reader tests exercise
local resume, missing/changed files, stale listing metadata, invalid TXT/EPUB
positions, damaged history and cancellation before display success.

The display fixture writes `home-*.pbm` previews for Full, Minimal, unset time,
empty/error history, long titles, Tools and multi-page tiles. These are local
inspection artifacts. The measured adjacent-tile focus update transfers
**3,456 bytes** of panel RAM data; unchanged submissions transfer none. This
measures driver traffic, not physical button latency or panel ghosting.

The E1.2 iteration passed all 32 Host targets, reader ASan/UBSan with connectivity
enabled and disabled, ShellCheck and both ESP-IDF 5.5.2 firmware builds. Visual
inspection confirmed focus, title truncation and status/footer separation in
the generated Home variants.

| Built artifact | Full | Minimal | Reduction |
| --- | ---: | ---: | ---: |
| Application binary | 3,000,736 bytes | 553,200 bytes | 81.6% |
| Static internal RAM | 213,535 bytes | 118,683 bytes | 44.4% |

The existing profile runner writes logs and `report.json` under
`build-profile-regression/`. Minimal still emits known Kconfig warnings for
defaults belonging to excluded IDF components; compilation and source-exclusion
checks pass. Device smoke was not run for this UI change. Physical display
contrast, ghosting and button latency remain panel measurements.

E1.6 passed all 33 Host targets, focused display integration, reader ASan/UBSan
with connectivity enabled and disabled, and Full/Minimal ESP32-S3 builds.
Rendered Full/Minimal and multi-page Home screens, focused icons, both reader
sizes, sleep covers, preview controls and blank privacy output were inspected.
The local previews are under `build-ui-e1-6/after/`; no device smoke or physical
low-light readability measurement was performed for this UI iteration.
