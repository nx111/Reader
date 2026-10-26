"""Compile and exercise the real MobiBook parser on a POSIX development machine.

Set LIBMOBI_SOURCE to an official libmobi source checkout containing tests/samples.
No downloads occur during tests. See tests/README.md for the pinned setup and
coverage limits. Set READER_TEST_SANITIZERS=address,undefined to instrument builds.
"""

import ctypes.util
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"


def _method(source, signature):
    """Retain the exact production formatting methods without Win32 threading/UI."""
    start = source.index(signature)
    # Both methods end with an unindented closing brace. No body rewriting.
    end = source.index("\n}", start) + 2
    return source[start:end]


def _palmdoc(payload):
    """Generate a minimal uncompressed TEXt/REAd database; payload is CP1252."""
    record0 = struct.pack(">HHIHHHH", 1, 0, len(payload), 1, 4096, 0, 0)
    offset = 78 + 2 * 8 + 2
    header = struct.pack(
        ">32sHHIIIIII4s4sIIH", b"Plain PalmDOC", 0, 0, 0, 0, 0, 0, 0, 0,
        b"TEXt", b"REAd", 0, 0, 2,
    )
    records = struct.pack(">II", offset, 0) + struct.pack(">II", offset + len(record0), 1)
    return header + records + b"\0\0" + record0 + payload


class MobiParserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = os.environ.get("LIBMOBI_SOURCE")
        if not source:
            raise unittest.SkipTest("Set LIBMOBI_SOURCE to run actual-parser integration tests (tests/README.md)")
        cls.source = Path(source).resolve()
        if not (cls.source / "src" / "mobi.h").is_file():
            raise RuntimeError("LIBMOBI_SOURCE must point at an official libmobi source checkout")
        if (cls.source / "src" / "mobi.h").read_bytes() != (ROOT / "opensrc" / "libmobi" / "inc" / "mobi.h").read_bytes():
            raise RuntimeError("The supplied libmobi header differs from the bundled ABI; use the pinned revision in tests/README.md")
        if not (cls.source / "tests" / "samples" / "sample-ncx.mobi").is_file():
            raise RuntimeError("LIBMOBI_SOURCE is missing the upstream test fixtures")
        cxx = shlex.split(os.environ.get("CXX", "g++"))
        cc = shlex.split(os.environ.get("CC", "gcc"))
        if not cxx or not cc or not shutil.which(cxx[0]) or not shutil.which(cc[0]):
            raise unittest.SkipTest("C and C++ compilers are required")
        xml_library = ctypes.util.find_library("xml2")
        if not xml_library:
            raise unittest.SkipTest("A native libxml2 runtime is required")

        cls.temporary = tempfile.TemporaryDirectory(prefix="reader-mobi-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.build = Path(cls.temporary.name)
        cls.executable = cls.build / "mobi-parser-test"
        cls.samples = cls.source / "tests" / "samples"
        sanitizers = os.environ.get("READER_TEST_SANITIZERS", "")
        flags = ["-O1", "-g", "-fno-omit-frame-pointer"]
        if sanitizers:
            flags.append("-fsanitize=" + sanitizers)

        # Build the dependency from supplied official sources; do not replace
        # the Windows libraries shipped in opensrc/libmobi/lib.
        dependency_sources = [
            "buffer", "compression", "debug", "index", "memory", "meta",
            "parse_rawml", "read", "structure", "util", "write", "opf", "xmlwriter",
        ]
        library = cls.build / "libmobi.so"
        subprocess.run(
            cc + flags + [
                "-shared", "-fPIC", "-I" + str(cls.source / "src"),
                "-DUSE_XMLWRITER", "-DHAVE_UNISTD_H", "-DHAVE_STRDUP",
                "-DMOBI_INLINE=inline", '-DPACKAGE_VERSION="test"',
            ] + [str(cls.source / "src" / (name + ".c")) for name in dependency_sources]
            + ["-lz", "-o", str(library)], check=True,
        )

        # GCC cannot preprocess MSVC's UTF-16 resource header. The parser source
        # is copied byte-for-byte; only resource.h is losslessly transcoded.
        parser = cls.build / "MobiBook.cpp"
        shutil.copyfile(ROOT / "Reader" / "MobiBook.cpp", parser)
        resource = ROOT / "Reader" / "resource.h"
        resource_bytes = resource.read_bytes()
        resource_encoding = "utf-16" if resource_bytes.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        (cls.build / "resource.h").write_text(resource_bytes.decode(resource_encoding), encoding="utf-8")

        book_source = (ROOT / "Reader" / "Book.cpp").read_text(encoding="utf-8")
        formatting = cls.build / "book-formatting.cpp"
        formatting.write_text(
            '#include "mobi_parser_compat.h"\n'
            '#define MAX_BLANK_LINE 2\n'
            '#define is_blank(c) ((c) == 0x20 || (c) == 0x09 || (c) == 0x0B || (c) == 0x0C || (c) == 0x3000 || (c) == 0xA0)\n'
            + _method(book_source, "BOOL Book::FormatText(") + "\n"
            + _method(book_source, "BOOL Book::GetLine(") + "\n", encoding="utf-8",
        )
        # On Linux the SONAME is accepted directly without a libxml2.so dev
        # symlink. macOS callers can use LIBXML2_LDFLAGS instead.
        xml_flags = shlex.split(os.environ.get("LIBXML2_LDFLAGS", "-l:" + xml_library))
        subprocess.run(
            cxx + flags + [
                "-std=c++11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                "-include", str(TESTS / "mobi_parser_compat.h"),
                "-I" + str(cls.build), "-I" + str(TESTS), "-I" + str(TESTS / "mobi_stubs"),
                "-I" + str(ROOT / "Reader"),
                "-I" + str(ROOT / "opensrc" / "libmobi" / "inc"),
                "-I" + str(ROOT / "opensrc" / "libxml2" / "inc"),
                str(parser), str(formatting), str(TESTS / "mobi_parser_test.cpp"),
                "-L" + str(cls.build), "-Wl,-rpath," + str(cls.build), "-lmobi",
            ] + xml_flags + ["-o", str(cls.executable)], check=True,
        )

    def run_parser(self, *arguments):
        result = subprocess.run(
            [str(self.executable), *map(str, arguments)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + "\n" + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_markup_entities_utf16_and_exact_nonnumeric_anchor_navigation(self):
        self.run_parser("synthetic")

    def test_normalized_utf8_without_meta_or_with_stale_charset(self):
        self.run_parser("encoding")

    def test_real_standalone_kf8(self):
        self.run_parser("load", self.samples / "sample-obfuscated-fonts.mobi", "ok")

    def test_real_hybrid_nested_toc(self):
        self.run_parser("load", self.samples / "sample-ncx.mobi", "ok")

    def test_real_hybrid_cover_resources(self):
        self.run_parser("load", self.samples / "sample-multimedia.mobi", "cover")

    def test_legacy_mobi_regression(self):
        self.run_parser("load", self.samples / "sample-cp1252.mobi", "cover")

    def test_hybrid_huffcdic_compression(self):
        self.run_parser("load", self.samples / "sample-unicode-huffdic.mobi", "ok")

    def test_hybrid_uncompressed_text(self):
        self.run_parser("load", self.samples / "sample-unicode-uncompressed.mobi", "ok")

    def test_real_palmdoc_textread(self):
        self.run_parser("load", self.samples / "sample-textread.mobi", "text")

    def test_supported_container_extension_aliases(self):
        for extension, fixture in (
            ("azw", "sample-ncx.mobi"),
            ("prc", "sample-cp1252.mobi"),
            ("pdb", "sample-textread.mobi"),
        ):
            for variant in (extension, extension.upper(), extension.capitalize()):
                with self.subTest(extension=variant):
                    filename = self.build / ("alias." + variant)
                    shutil.copyfile(self.samples / fixture, filename)
                    self.run_parser("load", filename, "text" if extension == "pdb" else "ok")

    def test_minimal_plain_palmdoc_pdb(self):
        text = b"First plain PalmDOC line.\nSecond plain PalmDOC line.\nA <not-a-tag> B &copy;\nCaf\xe9 plain text.\n"
        for prefix in (b"", b"<not-a-tag> literal first line </not-a-tag>\n", b"<htmlish> not an HTML document </htmlish>\n"):
            with self.subTest(prefix=prefix):
                filename = self.build / "plain.PDB"
                filename.write_bytes(_palmdoc(prefix + text))
                self.run_parser("load", filename, "plain")

        # PDB/PRC is a generic Palm database extension. An unrelated database
        # must be rejected, not decoded as an ebook or misreported as supported.
        unrelated = bytearray(filename.read_bytes())
        unrelated[60:68] = b"DATAothr"
        filename = self.build / "not-a-book.pdb"
        filename.write_bytes(unrelated)
        self.run_parser("load", filename, "reject")

    def test_html_document_in_palmdoc(self):
        html = b"<HTML><HEAD><TITLE>HTML PalmDOC title</TITLE></HEAD><BODY>" \
            b"<h1>HTML PalmDOC &amp; Caf\xe9</h1><p>literal &lt;safe&gt;</p></BODY></HTML>"
        for prefix in (b"", b" \r\n<?xml version='1.0' encoding='windows-1252'?>\n<!-- comment --><!DOCTYPE HTML>\n"):
            with self.subTest(prefix=prefix):
                filename = self.build / "html-document.pdb"
                filename.write_bytes(_palmdoc(prefix + html))
                self.run_parser("load", filename, "htmlpalm")

    def test_unicode_filename(self):
        filename = self.build / "阅读样例🙂.AZW3"
        shutil.copyfile(self.samples / "sample-ncx.mobi", filename)
        self.run_parser("load", filename, "ok")

    def test_encrypted_documents_rejected(self):
        for name in ("sample-drm-v1.mobi", "sample-drm_pidLTKULBB^5V-v2.mobi"):
            with self.subTest(name=name):
                self.run_parser("load", self.samples / name, "encrypted")

    def test_corrupt_documents_rejected(self):
        inputs = {
            "empty.azw3": b"",
            "truncated.azw3": (self.samples / "sample-ncx.mobi").read_bytes()[:100],
            "wrong-format.azw3": b"this is not a Kindle book",
        }
        for name, data in inputs.items():
            with self.subTest(name=name):
                filename = self.build / name
                filename.write_bytes(data)
                self.run_parser("load", filename, "reject")
        self.run_parser("load", self.samples / "sample-invalid-indx.fail", "reject")

    def test_encrypted_hybrid_legacy_part_rejected(self):
        # Default parsing selects KF8, but an encrypted KF7 companion must not
        # accidentally bypass the explicit no-DRM policy.
        data = bytearray((self.samples / "sample-ncx.mobi").read_bytes())
        offset = int.from_bytes(data[78:82], "big")
        data[offset + 12:offset + 14] = (2).to_bytes(2, "big")
        filename = self.build / "encrypted-hybrid.azw3"
        filename.write_bytes(data)
        self.run_parser("load", filename, "encrypted")

    def test_unknown_encryption_rejected(self):
        # Set the PalmDOC record0 encryption word to an unknown nonzero scheme;
        # never reinterpret ciphertext as text just because it is not DRM v1/v2.
        data = bytearray((self.samples / "sample-obfuscated-fonts.mobi").read_bytes())
        offset = int.from_bytes(data[78:82], "big")
        data[offset + 12:offset + 14] = (99).to_bytes(2, "big")
        filename = self.build / "unknown-encryption.azw3"
        filename.write_bytes(data)
        self.run_parser("load", filename, "encrypted")


if __name__ == "__main__":
    unittest.main()
