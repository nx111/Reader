"""Portable production search, excerpt, cancellation and stale-request tests."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReaderTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not shutil.which(compiler[0]):
            raise unittest.SkipTest("A C++ compiler is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="reader-search-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.executable = Path(cls.temp.name) / "reader-text-test"
        result = subprocess.run([*compiler, "-std=c++14", "-Wall", "-Wextra", "-g",
                                 *shlex.split(os.environ.get("READER_LIBRARY_TEST_FLAGS", "")),
                                 "-I" + str(ROOT / "Reader"), str(ROOT / "tests" / "reader_text_test.cpp"),
                                 "-o", str(cls.executable)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError(result.stdout)

    def run_case(self, case):
        result = subprocess.run([str(self.executable), case], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_literal_unicode_and_case_search(self):
        self.run_case("search")

    def test_exact_count_bounded_rows_empty_and_no_results(self):
        self.run_case("bounds")

    def test_cancel_with_no_hits_many_hits_and_long_query(self):
        self.run_case("cancel")

    def test_context_highlight_whitespace_and_utf16_boundaries(self):
        self.run_case("excerpt")

    def test_repeat_cancel_and_document_change_reject_stale_completion(self):
        self.run_case("gate")

    def test_chapter_labels_ignore_unloaded_ranges(self):
        self.run_case("chapters")

    def test_keyboard_results_query_cancel_button_and_tab_roles(self):
        self.run_case("keys")


if __name__ == "__main__":
    unittest.main()
