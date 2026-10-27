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
                     "Keyset.h", "Utils.h", "Upgrade.h", "DPIAwareness.h", "LibraryModel.h", "ReadingState.h", "BookmarkData.h", "SearchDlg.h", "ReaderText.h", "ThemePalette.h", "CommandIds.h", "WindowAppearance.h"]:
            shutil.copy2(ROOT / "Reader" / name, work / name)
        # POSIX exposes overloads for strcasestr; the untouched Windows declaration
        # is unused by these tests and cannot coexist with that host declaration.
        utils = work / "Utils.h"
        utils.write_text(utils.read_text().replace("char *strcasestr(const char *s, const char *find);", ""))
        shutil.copy2(ROOT / "tests" / "library_compat.h", work / "framework.h")
        (work / "shlwapi.h").write_text("#pragma once\n")
        reader = (ROOT / "Reader" / "Reader.cpp").read_text()
        binding = reader.split("static void RebindReadingCache()\n", 1)[1].split("void OpenLibraryBook", 1)[0]
        navigation = reader.split("static ReadingAnchor CurrentReadingAnchor()\n", 1)[1].split("static void NavigateBookmark(", 1)[0]
        search = reader.split("static ReaderSearchToken CurrentSearchToken()\n", 1)[1].split("void ClearReaderSearchHighlight()", 1)[0]
        batch = reader.split("int RemoveLibraryBooks(const std::vector<int>& ids)\n", 1)[1].split("BOOL RelinkLibraryBook", 1)[0]
        (work / "reader_bridges.inc").write_text(
            "static void RebindReadingCache()\n" + binding
            + "static ReadingAnchor CurrentReadingAnchor()\n" + navigation
            + "static ReaderSearchToken CurrentSearchToken()\n" + search
            + "int RemoveLibraryBooks(const std::vector<int>& ids)\n" + batch)
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

    def test_ui_schema_migration_backup_and_persistence_preserve_books(self):
        self.run_case("ui_persistence")

    def test_malformed_theme_or_sidebar_settings_preserve_original_cache(self):
        self.run_case("ui_invalid")

    def test_splitter_clamps_dpi_cancel_and_repeated_drag(self):
        self.run_case("splitter")

    def test_theme_color_contrast_and_custom_reading_semantics(self):
        self.run_case("palette")

    def test_many_imports_keep_active_identity_and_progress(self):
        self.run_case("large")

    def test_production_import_bridge_rebinds_live_reader_after_growth(self):
        self.run_case("import_bridge")

    def test_production_navigation_bridge_restores_and_rolls_back_failed_fetch(self):
        self.run_case("navigation_bridge")

    def test_bookmark_metadata_roundtrip_rebase_relink_and_old_cache(self):
        self.run_case("bookmark_metadata")

    def test_invalid_bookmark_metadata_never_overwrites_cache(self):
        self.run_case("bookmark_invalid")

    def test_notes_pdf_view_v4_migration_invalid_values_and_relink_preserve_data(self):
        self.run_case("notes_pdf")

    def test_batch_state_is_idempotent_and_removal_preserves_files_and_anchors(self):
        self.run_case("batch")

    def test_production_search_scope_and_stale_result_rejection(self):
        self.run_case("search_bridge")

    def test_production_batch_rebind_and_loading_book_cancellation(self):
        self.run_case("batch_bridge")


if __name__ == "__main__":
    unittest.main()
