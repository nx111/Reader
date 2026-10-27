"""Compile production cache/migration code against fault-injected Win32 stand-ins.

These portable checks do not replace MSVC/NTFS durability or Windows GUI tests.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LibraryStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not shutil.which(cls.compiler[0]):
            raise unittest.SkipTest("A C++ compiler is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="reader-library-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        for name in ["Cache.cpp", "Cache.h", "Jsondata.cpp", "Jsondata.h", "types.h",
                     "Keyset.h", "Utils.h", "Upgrade.h", "DPIAwareness.h", "LibraryModel.h", "ReadingState.h"]:
            shutil.copy2(ROOT / "Reader" / name, work / name)
        # POSIX exposes overloads for strcasestr; the untouched Windows declaration
        # is unused by these tests and cannot coexist with that host declaration.
        utils = work / "Utils.h"
        utils.write_text(utils.read_text().replace("char *strcasestr(const char *s, const char *find);", ""))
        shutil.copy2(ROOT / "tests" / "library_compat.h", work / "framework.h")
        (work / "shlwapi.h").write_text("#pragma once\n")
        reader = (ROOT / "Reader" / "Reader.cpp").read_text()
        binding = reader.split("static void RebindReadingCache()\n", 1)[1].split("void OpenLibraryBook", 1)[0]
        navigation = reader.split("static ReadingAnchor CurrentReadingAnchor()\n", 1)[1].split("BOOL GetReadingRect", 1)[0]
        (work / "reader_bridges.inc").write_text(
            "static void RebindReadingCache()\n" + binding
            + "static ReadingAnchor CurrentReadingAnchor()\n" + navigation)
        source = ROOT / "opensrc" / "cjson"
        cls.executable = work / "library-test"
        flags = shlex.split(os.environ.get("READER_LIBRARY_TEST_FLAGS", ""))
        result = subprocess.run([*cls.compiler, "-std=c++14", "-Wall", "-Wextra", "-g", *flags,
                                 "-I" + str(work), "-I" + str(source), "-I" + str(ROOT / "tests"),
                                 str(work / "Cache.cpp"), str(work / "Jsondata.cpp"), str(source / "cJSON.c"),
                                 str(ROOT / "tests" / "library_state_test.cpp"), "-o", str(cls.executable)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError(result.stdout)

    def run_case(self, name):
        result = subprocess.run([str(self.executable), name], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_encoded_legacy_migration_backup_order_marks_and_unknown_time(self):
        self.run_case("migration")

    def test_malformed_future_and_truncated_caches_are_preserved(self):
        self.run_case("invalid")

    def test_stable_identity_reorder_removal_and_clear(self):
        self.run_case("identity")

    def test_normalized_path_and_file_identity_deduplication(self):
        self.run_case("dedup")

    def test_relink_merge_preserves_source_anchor_and_all_marks(self):
        self.run_case("relink")

    def test_failed_writes_backup_flush_replace_and_external_change(self):
        self.run_case("persistence")

    def test_library_filters_and_unicode_search(self):
        self.run_case("filters")

    def test_navigation_live_anchor_branching_and_chapter_relative_positions(self):
        self.run_case("navigation")

    def test_geometry_drawer_wide_focus_and_100_to_200_percent_dpi(self):
        self.run_case("geometry")

    def test_many_imports_keep_active_identity_and_progress(self):
        self.run_case("large")

    def test_production_import_bridge_rebinds_live_reader_after_growth(self):
        self.run_case("import_bridge")

    def test_production_navigation_bridge_restores_and_rolls_back_failed_fetch(self):
        self.run_case("navigation_bridge")


if __name__ == "__main__":
    unittest.main()
