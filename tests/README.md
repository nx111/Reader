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

The 12 library tests compile production `Cache.cpp`/`Jsondata.cpp` with bundled
cJSON and fault-injectable Win32 substitutes. They also compile the actual
reader import/rebinding and reading-navigation bridge functions, plus the
production filter/sort/history/geometry helpers. Coverage includes encoded v1
migration, unchanged order/marks/unknown dates, corrupt/future/partial caches,
backup/flush/replace failure retry, external writers, stable IDs after reorder or
clear, path/file-ID deduplication, relink collision merging/capacity rollback,
Unicode search, sorting, 20,000 entries, live reader rebinding and asynchronous
chapter-return failure rollback. Geometry is checked at 96/120/144/192 DPI.

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
