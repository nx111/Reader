# Portable regression tests

The application and bundled `.lib` files target Windows/MSVC. These tests do not
claim to replace a Windows x86/x64 build or GUI/GDI+ test.

## File recognition (no dependencies)

```sh
python -m unittest discover -s tests -p 'test_reader_formats.py' -v
```

This compiles the production extension predicates and checks the Open dialog,
validation, command-line/history dispatch, mixed-case extensions, and both network
and offline configurations.

## Production MOBI-family/PalmDOC parser

Use an official libmobi checkout, including its upstream sample fixtures:

```sh
git clone https://github.com/bfabiszewski/libmobi.git /tmp/libmobi
git -C /tmp/libmobi checkout 906274205c11944b628da1c553b255acb1af7c55
LIBMOBI_SOURCE=/tmp/libmobi python -m unittest discover -s tests -p 'test_*.py' -v
```

Requirements: Python 3, GCC/G++ (or compatible `CC`/`CXX`), zlib headers/library,
and native libxml2 runtime. The test runner does not download anything. It builds
libmobi into a temporary directory with its internal XML writer and encryption
disabled, and compiles a byte-identical copy of production `MobiBook.cpp` plus the
unchanged `Book::FormatText`/`Book::GetLine` methods. The shipped libmobi header is
used for the application-side compile. Resource headers are losslessly converted
from UTF-16 to UTF-8 for GCC preprocessing. All temporary binaries are removed.

`mobi_parser_compat.h` substitutes the Win32 base class/threading, UTF-8 conversion
(with UTF-16 surrogate-unit indexing), and image-stream/GDI+ interfaces. The image
stub checks resource signatures, **not actual image decoding, pixels or layout**.
Parsing, libxml2 DOM operations, formatting, OPF/NCX handling, chapter construction,
libmobi KF8 reconstruction, and failure cleanup use production code.

Coverage includes synthetic nonnumeric/named/Unicode anchors, repeated heading
text, nested TOC, HTML entities and UTF-16 indexing, a one-character body, missing
TOC, empty body, real standalone KF8, hybrid KF7/KF8, cover extraction, legacy
MOBI, Unicode filenames, repeated success/failure/success loads, DRM v1/v2,
unknown encryption, malformed/truncated files, a corrupt index fixture, HUFF/CDIC
and uncompressed hybrid text, real textread/PalmDOC and generated plain-text
PalmDOC PDB (literal angle brackets/ampersands, CP1252 accents, and HTML-document
detection boundaries), rejection of unrelated Palm databases, AZW/PRC/PDB alias extensions
in lower/upper/mixed case, optional malformed/missing NCX, duplicate TOC
destinations, missing spine references and oversized/null reconstructed parts.
Indentation normalization covers both NBSP and mixed ideographic/ASCII blanks.
Normalized UTF-8 Chinese/emoji titles, bodies and IDs are checked without a meta
charset, with an XML declaration only, and with stale windows-1252 metadata.

Sanitizer run (where supported):

```sh
LIBMOBI_SOURCE=/tmp/libmobi READER_TEST_SANITIZERS=address,undefined \
  python -m unittest discover -s tests -p 'test_*.py' -v
```

Upstream samples stay in the separate libmobi checkout and retain their original
licenses; no third-party ebook binaries are copied into this repository.

Some sandbox runners use ptrace, which prevents LeakSanitizer from starting. In
that environment use `ASAN_OPTIONS=detect_leaks=0` with the sanitizer command;
address/undefined-behavior checks remain enabled, but **leak checking is not
verified**. Do not interpret that run as a LeakSanitizer pass.

## Library, migration, navigation and layout

```sh
python -m unittest discover -s tests -p 'test_library_state.py' -v
```

The 21 library tests compile production `Cache.cpp`/`Jsondata.cpp` with bundled
cJSON and fault-injectable Win32 substitutes. They also compile the actual
reader import/rebinding and reading-navigation bridge functions, plus the
production filter/sort/history/geometry helpers. Coverage includes encoded v1
migration, unchanged order/marks/unknown dates, corrupt/future/partial caches,
backup/flush/replace failure retry, external writers, stable IDs after reorder or
clear, path/file-ID deduplication, relink collision merging/capacity rollback,
Unicode search, sorting, 20,000 entries, live reader rebinding and asynchronous
chapter-return failure rollback. Geometry is checked at 96/120/144/192 DPI. Additional tests cover v2-to-v3
backup, sparse chapter/summary persistence, malformed metadata, batch state
idempotence and compaction, active/loading book cancellation, actual local vs
current-online-chapter search scope, stale result rejection, and keyboard roles.

```sh
READER_LIBRARY_TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  ASAN_OPTIONS=detect_leaks=0 \
  python -m unittest discover -s tests -p 'test_library_state.py' -v
```

File operations are an in-memory test double, not an NTFS durability test; the
layout checks are mathematical, not actual Windows font/GDI/control/focus tests.
See [the manual Windows checklist](../doc/library-usability.md).

Optional Linux MinGW API/type syntax checks (compiler must already be installed):

```sh
python tests/check_windows_syntax.py --network
python tests/check_windows_syntax.py
```

Use `--compiler /path/to/x86_64-w64-mingw32-g++` and, if needed,
`--windows-include /path/to/mingw/include` for a local toolchain. These checks
include both networking configurations of all changed translation units.
`-fpermissive`, temporary header-name aliases and an MSVC-style `max()` adapter
accommodate existing MSVC-specific source; they do not certify MSVC linkage.

## Search and text excerpts

```sh
python -m unittest discover -s tests -p 'test_reader_text.py' -v
```

Seven tests compile the production literal search/excerpt/request-gate/key
helpers. They cover exact counts with bounded display rows, empty/no results,
Unicode and case behavior, UTF-16 excerpt boundaries, cancellation with zero or
many hits and long terms, duplicate/stale requests, chapter labels and native
keyboard role dispatch. `READER_LIBRARY_TEST_FLAGS` also enables sanitizers for
this harness. These checks do not simulate actual Windows control focus, GDI
highlight pixels, screen readers, or display scaling.

## Sidebar, theme and rendering boundaries

```sh
python -m unittest discover -s tests -p 'test_reader_ui.py' -v
```

Nine additional checks compile actual production `OnPaint`, `Invalidate`,
`SyncCurrentChapter`, `CalcPageDown` and `GdiSurface` against GDI/window stand-ins,
and validate the source contracts for scroll routing, capture/cancel, theme
coverage, resources, project registration and unchanged workflow build inputs.
The executable cases cover body-local copies, body-only invalidation, stable
chapter scroll/selection, too-small reflow anchors, and 200 iterations of buffer
reuse including allocation failure and safe deselection. They are not real
Windows GDI, focus, accessibility or flicker tests. The library suite additionally
checks v3-to-v4 backup/persistence, malformed UI settings, 96/120/144/168/192 DPI
splitter boundaries and light/dark palette contrast/custom reading semantics.
`READER_LIBRARY_TEST_FLAGS` also enables sanitizers for these checks.

The previous sidebar/theme portable suite was 57 tests: 20 existing format/parser tests,
21 cache/library/production-bridge tests, 7 search/text tests and 9 UI/rendering
tests. See [the current local verification record](../doc/sidebar-theme-render.md)
for the Windows runtime limits and manual checklist.


## Final left navigation, bookmark notes/export and PDF

```sh
python -m unittest discover -s tests -p 'test_navigation_ui.py' -v
READER_PDFIUM_LIBRARY=/path/to/verified/libpdfium.so \
  LIBMOBI_SOURCE=/path/to/libmobi python -m unittest discover -s tests -p 'test_*.py' -v
```

The final suite is **81 tests**, replacing the abandoned top-menu layout checks:
20 format/parser, 22 cache/library/production bridges, 7 search/text, 9 existing
render/UI, 15 left navigation/layout/icon/export/PDF semantics, and 8 real PDFium
engine checks. No tests download a PDF library. `test_pdf_engine.py` checks the
exact dependency-lock DLL/shared-library checksum before executing the renderer;
without a supplied verified library those 8 tests are explicitly skipped. The
optional password fixture uses already installed `pypdf`; it is not an application
dependency. Other PDF fixtures are generated directly from small PDF objects.

The production engine is executed on Linux for real text and scan-page pixels,
metadata/outlines, searchable text/rectangles, malformed/password files, cancellation,
allocation bounds and repeat/parallel open-render-close. The UI cases cover final
left navigation, icon rail/drawer, one docked library navigation, wrapped actions,
Alt/F10/AltGr/Shift+F10/keyboard routes, same-page PDF navigation anchors, fit/zoom/
resize at 100–200% mathematical DPI, bookmark note migration/invalid values/relink,
and local Markdown/TXT output. Source contracts check native controls/popups,
focus/cancel, stale PDF results, thread cleanup and body-only drawing isolation.
They do not execute a Windows event loop or assert actual GUI accessibility/pixels.

The full 81 pass with ASan/UBSan in the local implementation environment (LSan off).
The precompiled PDFium library itself is not sanitizer-instrumented. The optional
MinGW script covers 24 translation units in network and offline configurations;
resource compilation uses a lossless temporary UTF-16 conversion and SDK case
aliases. No MSVC linkage, native PowerShell or new Windows CI build was run here.
Final package size and real Windows/DPI/transparent/flicker behavior remain untested.
See [implementation, dependency provenance and manual checklist](../doc/left-navigation-pdf.md).

## Focus exit and internal list padding (local regression)

```sh
python -m unittest discover -s tests -p 'test_focus_ui.py' -v
```

Eight additional cases bring the current suite to **89 tests**. They compile the
production focus-state and Escape functions against small native-window stand-ins,
including cancel priorities, repeat/release, foreign windows and 200 restore cycles.
Geometry/palette checks cover a permanent 48-DIP rail without a duplicate header
exit, one named focus-toggle command, header-free scrolling, activation/focus/menu/capture,
light/dark contrast and 100–200% DPI padding with narrow-column clamps. Source
contracts preserve native text/hit/scroll bounds, local repaint and the absence of
edge/hide timers. The rail is an opaque app child sharing the effective reading background;
focus body opacity is explicitly preserved. Focus cases run with
ASan/UBSan (LSan off).
These are not Windows GUI, touch, screen-reader or actual DPI/flicker tests; see
[the implementation and Windows checklist](../doc/focus-rail-padding.md).

## Hidden scrollbars, in-app focus colors and Ctrl wheel

```sh
python -m unittest discover -s tests -p 'test_scrollbar_ui.py' -v
```

Ten additional cases bring the current suite to **99 tests**. Production navigation layout/emphasis/palette selection, text zoom and native
tracking forwarding execute against small Win32 substitutes; scrollbar range/visibility/palette/wheel helpers execute
directly. Cases cover 32-bit limits, native thumb hit bounds, fade/reentry/capture,
200 repeated drags and focus switches, unchanged native parent,
minimize/focus restoration, background matching/high contrast, no owned window or
region creation, 100–200% DPI, point bounds, fractional deltas, scope, book switches,
unchanged text anchors and opaque focus body. Both configurations now check 25
translation units, plus resource compilation. No Windows desktop, native tracking,
UIA/screen reader, touch, DWM pixels, AltTab or actual flicker has been run here.
See [implementation and runtime checklist](../doc/scrollbar-focus-zoom.md).

## Targeted fixed-opacity checks

The latest user preference is to test only affected paths. For the fixed-opacity
change run the 7 checks documented in [opaque-window.md](../doc/opaque-window.md);
do not treat the previous 99-case record as a new full-suite run. Cache tests
reuse the production cache harness but execute only opacity normalization, legacy
migration/data preservation and invalid-cache protection. Changed Reader/Cache/Page
units receive syntax checks in both configurations. No unrelated format/PDF/library
UI tests or sanitizer matrix were run for this change; resources are unchanged.

## Reading content insets (targeted)

The main navigation icon inset is the shared 14 DIP metric. The reading child
uses twice its once-scaled pixel value on each side (28 px at 96 DPI, 56 px at
192 DPI), independent of text size. Old cached borders are retained as data but
do not override the uniform default. Only a new explicit edit to the margin
field enables custom borders; saving other display options keeps automatic
insets. The optional `ui.reading_border_custom` flag records this distinction
without changing existing book or margin data. Text pagination, cover placement, PDF
fit/zoom and the native editor consume the same content rectangle. Very small
viewports shrink all four automatic insets equally to leave a pixel of content.

Run only the directly affected checks from `tests/`:

```sh
python -m unittest -v \
  test_reader_ui.PaintBoundaryTests.test_actual_paint_uses_child_local_bounds_and_preserves_anchor \
  test_reader_ui.PaintBoundaryTests.test_actual_pagination_keeps_anchor_when_reflow_cannot_fit_a_glyph \
  test_reader_ui.PaintBoundaryTests.test_actual_pagination_uses_inset_dimensions_once_in_both_directions \
  test_reader_ui.UiSourceContractTests.test_reading_insets_align_text_cover_pdf_and_editor_without_extra_margins \
  test_navigation_ui.NavigationLayoutTests.test_reading_insets_ignore_legacy_borders_until_explicit_edit \
  test_navigation_ui.NavigationLayoutTests.test_pdf_fit_zoom_scroll_and_resize_preserve_normalized_anchor \
  test_library_state.LibraryStateTests.test_reading_border_legacy_defaults_and_explicit_edits_preserve_cache
```

These are portable geometry, production pagination/paint bridge and source
contract checks. They do not verify actual Windows font rendering, edit-control
formatting or PDF pixels on screen.

## Native title-bar theme (targeted)

```sh
python -m unittest discover -s tests -p test_titlebar_theme.py -v
python tests/check_windows_syntax.py --units Theme TitleBarTheme
python tests/check_windows_syntax.py --units Theme TitleBarTheme --network
```

The five checks compile the unchanged production `TitleBarTheme.cpp` against
Win32 API substitutes. They cover repeated light/dark and captionless/restored
mode changes, matching caption/text colors, high-contrast entry/exit and failed
accessibility queries, missing system DLL/export, rejected color attributes,
paired rollback, top-level-only application and theme/event/project wiring.
Only the documented DWM export is resolved, from the system directory.
The native code changes no client geometry, hit testing or opacity. These are
API-contract and source-boundary checks, not Windows 10/11 GUI, DWM pixels,
caption-button accessibility or DPI visual verification.

## Library row selection (targeted)

```sh
python -m unittest discover -s tests -p test_library_row_paint.py -v
python tests/check_windows_syntax.py --units Theme
python tests/check_windows_syntax.py --units Theme --network
```

Four checks compile the production cell/row painters against list-view/GDI
substitutes. They verify a continuous full-width selected background and one
row focus cue across 100–200% DPI, horizontal scroll and narrow/wide clients;
column fonts/text/padding; native multi-selection and focus without changing
item state; hidden keyboard cues; disabled/high-contrast palette use; invalid
row fallback and unchanged drawing of other padded lists. The library handles
item-prepaint as one unit and suppresses native focus postpaint. This follows
the [documented custom-draw protocol](https://learn.microsoft.com/en-us/windows/win32/controls/about-custom-draw).
These checks do not verify actual Windows list-view pixels, UIA or keyboard
interaction on a desktop.
