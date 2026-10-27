"""Real, hash-pinned PDFium rendering on the host; not a Windows GUI test.

No test downloads or runtime network. Set READER_PDFIUM_LIBRARY to the already
verified no-V8 shared library. The production engine code, not a mock, is built.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def fixture(path):
    objects = [b"<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R >>",
               b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
               b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 600] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
               b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 600] /Resources << /XObject << /Im1 8 0 R >> >> /Contents 7 0 R >>",
               b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    def stream(data, attributes=b""):
        return b"<< /Length " + str(len(data)).encode() + b" " + attributes + b">>\nstream\n" + data + b"\nendstream"
    objects += [stream(b"BT /F1 18 Tf 20 540 Td (Searchable page: Reader PDF) Tj ET"),
                stream(b"q 240 0 0 420 30 120 cm /Im1 Do Q"),
                stream(bytes([255,0,0, 0,255,0, 0,0,255, 20,20,20]), b"/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 "),
                b"<< /Title (Reader PDF regression) /Author (Reader tests) >>",
                b"<< /Type /Outlines /First 11 0 R /Last 12 0 R /Count 2 >>",
                b"<< /Title (Text) /Parent 10 0 R /Next 12 0 R /Dest [3 0 R /Fit] >>",
                b"<< /Title (Scan) /Parent 10 0 R /Prev 11 0 R /Dest [4 0 R /Fit] >>"]
    data = b"%PDF-1.4\n"; offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(data)); data += str(i).encode() + b" 0 obj\n" + obj + b"\nendobj\n"
    start = len(data); data += b"xref\n0 13\n0000000000 65535 f \n"
    data += b"".join(f"{x:010d} 00000 n \n".encode() for x in offsets[1:])
    data += b"trailer\n<< /Size 13 /Root 1 0 R /Info 9 0 R >>\nstartxref\n" + str(start).encode() + b"\n%%EOF\n"
    path.write_bytes(data)


class PdfEngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = os.environ.get("READER_PDFIUM_LIBRARY")
        if not cls.library:
            raise unittest.SkipTest("Set READER_PDFIUM_LIBRARY to a verified local PDFium binary")
        # Exact app dependency revision. Reject a substituted binary before dlopen.
        lock = __import__("json").loads((ROOT / "opensrc/pdfium/dependency.json").read_text())
        expected = lock["linux-x64"]["library_sha256"]
        if hashlib.sha256(Path(cls.library).read_bytes()).hexdigest() != expected:
            raise AssertionError("PDFium shared library checksum mismatch; refusing to execute")
        cls.temp = tempfile.TemporaryDirectory(prefix="reader-pdf-"); cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name); cls.pdf = cls.work / "文本与扫描.pdf"; fixture(cls.pdf)
        (cls.work / "invalid.pdf").write_bytes(b"%PDF broken reader regression")
        cls.exe = cls.work / "pdf-test"
        result = subprocess.run([*shlex.split(os.environ.get("CXX", "g++")), "-std=c++14", "-g", "-pthread",
                                 *shlex.split(os.environ.get("READER_LIBRARY_TEST_FLAGS", "")),
                                 "-I" + str(ROOT / "Reader"), "-I" + str(ROOT / "opensrc/pdfium/include"),
                                 str(ROOT / "Reader/PdfEngine.cpp"), str(ROOT / "tests/pdf_engine_test.cpp"), "-ldl", "-o", str(cls.exe)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode: raise AssertionError(result.stdout)

    def run_case(self, case, pdf=None, library=None):
        result = subprocess.run([str(self.exe), case, str(pdf or self.pdf), library or self.library],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_real_text_page_pixels_metadata_outline_and_search_boxes(self): self.run_case("text")
    def test_real_scan_page_renders_without_fabricated_text(self): self.run_case("scan")
    def test_cancelled_generation_does_not_render(self): self.run_case("cancel")
    def test_viewport_allocation_is_bounded(self):
        self.run_case("bounds")
        large=self.work/'oversized.pdf'
        with large.open('wb') as out: out.truncate(1 << 32)  # sparse; no 4 GiB allocation
        self.run_case("large", large)
    def test_repeat_switch_close_parallel_import_and_render(self): self.run_case("repeat")
    def test_corrupt_file_reports_format_error(self): self.run_case("invalid", self.work / "invalid.pdf")
    def test_missing_component_is_explicit(self): self.run_case("missing", library=str(self.work / "missing.so"))
    def test_password_protected_file_reports_password_error(self):
        try:
            from pypdf import PdfReader, PdfWriter
        except ImportError:
            self.skipTest("Optional pypdf is required to create the password fixture")
        writer = PdfWriter(); writer.append(PdfReader(self.pdf)); writer.encrypt("reader-secret")
        encrypted = self.work / "password.pdf"
        with encrypted.open("wb") as out: writer.write(out)
        self.run_case("password", encrypted)
