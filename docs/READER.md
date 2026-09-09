# Pocket reader

L1.2 adds **BOOK READER** to the Launcher. It reads UTF-8 TXT and basic,
unencrypted EPUB from the Storage-owned book library. The persistent status
bar, application lifecycle and DisplayService refresh policy also apply to
the reader.

## Install books

The `books` SPIFFS partition occupies 4 MiB at `0x912000`. Factory, OTA, OTA
metadata and NVS retain their addresses. Install the new partition table with
the normal complete firmware flash when upgrading from a pre-reader build.
An application-only OTA cannot add the partition table entry.

The build produces `build/books.bin` from the repository's `books/` directory.
It contains an original reading guide. To install that library:

```bash
source tools/activate-dev-env.sh
bash tools/build-firmware.sh
idf.py -p /dev/cu.usbmodem14301 flash
idf.py -p /dev/cu.usbmodem14301 books-flash
```

Replace the port with the connected Note4's port. `books-flash` replaces the
whole book partition with the selected directory. Regular `flash`, the device
smoke test and application OTA preserve it. Mount errors never format it.

To use your own directory of books:

```bash
idf.py -D "ZECTRIX_BOOKS_DIR=/absolute/path/to/books" build
idf.py -p /dev/cu.usbmodem14301 books-flash
```

Use a flat directory of `.txt` and `.epub` files, with UTF-8 filenames no longer
than 63 bytes. Extensions are case insensitive. The library shows the first
32 eligible names in byte-sorted order and reports when more exist. Directories,
other extensions and overlong names are skipped. Leave space for SPIFFS metadata;
the image generator reports insufficient capacity. The source directory setting
is retained by CMake. Reset it with
`idf.py -D "ZECTRIX_BOOKS_DIR=$PWD/books" reconfigure`.

For an existing generated `sdkconfig`, select an 8192-byte main-task stack and
96-byte SPIFFS object names, as in `sdkconfig.defaults`, then rebuild. SPIFFS
images and firmware must use the same object-name setting. The larger task stack
provides headroom for the measured bookmark persistence and filesystem call
chains; parsing does not create a worker task.

L1.3 adds **SEND BOOKS** for browser upload, download and deletion over local
Wi-Fi. Initialize a new content partition with the explicit installation above,
then add individual files without replacing the library. The reader closes its
file before the transfer session takes Storage ownership. See
[BOOK_TRANSFER.md](BOOK_TRANSFER.md). L1.4 adds the
[sleep dashboard](SLEEP_COVER.md), which reads the latest committed bookmark
after Reader exit. Shutdown presents the chosen cover, with a white fallback
on display failure, before the established peripheral cleanup sequence.

## Reading controls

| Scene | UP / DOWN | OK click | Hold OK |
| --- | --- | --- | --- |
| Library | Select book | Open / retry library | Home |
| Reading | Previous / next page | Reading options | Library, including during loading |
| Options | Select option | Change font, apply phone position, restart or save and return | Reading |

Hold DOWN retains global shutdown. Reading options use 16px or 24px glyphs;
changing size reflows from the current text anchor. Common CJK characters wrap
by character with opening/closing punctuation handling. Latin words wrap at
word boundaries; a word wider than a line splits at Unicode scalar boundaries.
Paragraphs use a two-character indent. TXT accepts LF and CRLF. Invalid UTF-8
uses replacement glyphs without splitting valid characters.

The page body is 384 by 216 pixels below the status/title bars. Initial pages
and scene changes request Quality; page turns request Fast. DisplayService
decides whether a partial or full refresh is needed. Unchanged idle callbacks
do not redraw. Status-only updates preserve book content.

## Streaming and format limits

`Engine::Open` returns Pending for EPUB metadata. Repeated `Poll` calls complete
the ZIP/container/OPF work; `Seek` then lays out the requested page. The owner
uses one RTOS tick between pending slices and its normal 250ms wait while idle.
Directory lookups, metadata, hidden XHTML text, DEFLATE blocks and backward
pagination all yield; Back and shutdown remain available.

The engine retains one 32 KiB inflate dictionary, one 1 KiB input buffer, two
640-glyph page buffers, 64 recent page anchors and at most 128 spine entries.
It never allocates a book or complete chapter. Individual source reads are at
most 1045 bytes. Inflate consumes one input block per call, including when a
stream contains many empty blocks. Container/OPF metadata is limited to 256 KiB
each, XHTML chapters to 16 MiB of uncompressed bytes, paths to 191 bytes, and
tags to 1023 bytes. These are parser memory/work bounds.

TXT resume and cached backward turns reconstruct context near the requested
UTF-8 offset. EPUB seeks replay the containing compressed chapter. Backward
navigation beyond recent anchors rescans pages cooperatively. No whole-book
page count or disk pagination cache is required. Progress shows the approximate
uncompressed-byte position at the end of the current page, reaching 100% at EOF;
it can change when the same anchor is reflowed at another font size.

Supported EPUB features include stored and raw-DEFLATE ZIP members, data
descriptors, namespace-qualified container/OPF tags, relative and percent-encoded
paths, spine order, block text and common/numeric entities. Head/style/script,
SVG and ruby pronunciation text are skipped. ZIP size and CRC checks remain in
place. Partial reads and corrupt streams report an error without replacing a
successfully saved position.

There is no CSS layout, image rendering, embedded-font loading, fixed-layout
support, navigation-link handling, complex-script shaping, ZIP64 or DRM support.
Encrypted ZIP members and EPUBs with `META-INF/encryption.xml` are rejected,
including font-obfuscated EPUBs. Other unsupported Unicode glyphs use the
replacement bitmap. See the [font notice](../components/zectrix_reader/font/README.md).

## Saved positions and phone sync

The filename is the book ID; source length also has to match when restoring a
position. Give a replacement edition a new filename, including when it has the
same byte length. Positions store a chapter and source-byte offset, independent
of page numbers and font size.

The eight most recently saved books and the latest outgoing revision are
committed together through Storage's `reader.marks` NVS blob. A successful
display commit saves the visible page start, font and progress. Display failure
does not advance the bookmark; the reader requests a recovery render and saves
after it succeeds. Identical saves do not write NVS. Save failures
are visible, retried on idle and retried on exit; reading remains available.
An invalid stored record is reported and left intact.

The latest reading position uses C1 durable key `0x0101`. Local persistence
precedes enqueue, so a reboot between those writes replays the same revision.
Already queued or acknowledged revisions succeed without creating another
update. C1 retains pending data across BLE loss and owns ACK/cursor persistence.
This key coalesces the latest reading activity; it does not synchronize all eight
local bookmarks as separate keys.

Android displays the received book and progress. **Resume this position** queues
a revision through its existing durable owner. Note4 offers **USE PHONE POSITION**
only for the open book with matching source length and a valid chapter/offset.
Receiving data never turns a page. Applying it is an explicit local action and
is saved after display success. Removing the trusted phone resets the reader's
incoming cursor while retaining local book positions for the next phone.

The payload layout is documented in the
[connectivity contract](CONNECTIVITY_CONTRACT.md#reader-progress).

## References and verification

CrossPoint's [EpubReaderActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp)
informs chapter/page loading and position preservation. Its
[web server](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp)
informs L1.3 file transfer. Its
[sleep activity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp)
informs the L1.4 ambient cover. Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
informs the Library → Reading → Options hierarchy and Back propagation. These
are independently implemented adaptations on the existing single owner and
bounded viewport scheduler; upstream application code is not linked or copied.

Run `bash tools/test-host.sh` for all Host targets, or
`ZECTRIX_READER_SANITIZE=1 bash tools/test-reader.sh` for focused ASan/UBSan checks.
Tests cover real file reads, compressed EPUB fixtures, malformed data, Unicode
pagination, cancellation during metadata/body decoding, scene controls, display
failure, NVS adapter errors and replay through the production C1 SyncEngine.
A 2 MiB TXT tail-resume fixture reads 63 source bytes. UI integration tests check
glyph pixels, font reflow and status preservation; set `ZECTRIX_UI_PREVIEW_DIR`
when running `tools/test-display-service.sh` to render PBM previews. Android's
JVM tests check the same byte layout and durable revision handling.

The L1.2 iteration passed all 28 Host targets with reader ASan/UBSan checks,
28 Android JVM tests and the debug build, and the ESP32-S3 firmware build.
Connected-device flash/boot smoke also passed. Book installation, physical reader
button navigation and BLE progress exchange were not exercised on hardware.
