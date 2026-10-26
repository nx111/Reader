"""Portable regression tests for the Win32 reader's file-format entry points.

Run with: python -m unittest discover -s tests -p 'test_*.py'
The C++ checks compile the actual predicates from Reader.cpp, without requiring
Windows UI dependencies or duplicating its extension allow-list in production.
"""

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Reader" / "Reader.cpp").read_text(encoding="utf-8")


class ReaderFormatTests(unittest.TestCase):
    def test_all_entry_points_use_shared_validation(self):
        drop = SOURCE.split("LRESULT OnDropFiles(", 1)[1].split("LRESULT OnFindText(", 1)[0]
        validate = SOURCE.split("BOOL IsVaildFile(", 1)[1].split("void OnOpenBook(", 1)[0]
        self.assertIn("!IsSupportedBookExtension(ext)", drop)
        self.assertIn("!IsSupportedBookExtension(ext)", validate)
        self.assertIn("IsVaildFile(hWnd, szFileName, &size)", SOURCE)
        # Both initial launch and forwarded command-line opens validate first;
        # history dispatches via the same OnOpenBook factory.
        self.assertGreaterEqual(SOURCE.count("IsVaildFile(NULL, fullname, NULL)"), 2)
        self.assertIn("OnOpenBook(hWnd, item->file_name, forced)", SOURCE)

    def test_failure_message_lifetime_fallback_and_retry(self):
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("A C++ compiler is required for the portable failure-flow check")
        failure = SOURCE.split("LRESULT OnOpenBookResult(", 1)[1].split("// create new item", 1)[0]
        # Only the success branch uses item; keep the failure branch unchanged.
        failure = failure.replace("    item_t *item = NULL;\n", "")
        harness = r'''
#include <cassert>
#include <cwchar>
#include <string>
using TCHAR = wchar_t;
using UINT = unsigned;
using HWND = void *;
using BOOL = bool;
using LRESULT = long;
#define FALSE false
#define TRUE true
#define MAX_PATH 260
#define _tcscpy std::wcscpy
const unsigned IDS_OPEN_FILE_FAILED = 100;
const unsigned IDS_ERROR = 101;
const unsigned MB_OK = 0;
const unsigned MB_RETRYCANCEL = 5;
const unsigned MB_ICONERROR = 16;
const int IDRETRY = 4;
enum book_type_t { book_text, book_mobi, book_online, book_web };
static bool alive;
static int errorReads;
static int retryCount;
static int boxResult;
static unsigned shownMessage;
static unsigned shownType;
static std::wstring retryFile;
class FailedBook {
public:
    FailedBook(unsigned error, book_type_t type) : error_(error), type_(type) { alive = true; }
    ~FailedBook() { assert(errorReads == 1); alive = false; }
    const wchar_t *GetFileName() { assert(alive); return L"C:\\books\\example.AZW3"; }
    book_type_t GetBookType() { assert(alive); return type_; }
    unsigned GetOpenErrorMessage() { assert(alive); ++errorReads; return error_; }
private:
    unsigned error_;
    book_type_t type_;
};
static FailedBook *_Book;
static void StopLoadingImage(HWND) {}
static void UpdateTitle(HWND) {}
static void Invalidate(HWND, BOOL, BOOL) {}
static void OnOpenBook(HWND, TCHAR *file, BOOL forced) {
    assert(!alive && _Book == nullptr);
    assert(!forced);
    ++retryCount;
    retryFile = file;
}
static int MessageBox_(HWND, unsigned message, unsigned title, unsigned type) {
    assert(!alive && _Book == nullptr);
    assert(title == IDS_ERROR);
    shownMessage = message;
    shownType = type;
    return boxResult;
}
LRESULT OnOpenBookResult(''' + failure + r'''
    return TRUE;
}
static void check(unsigned error, book_type_t type, int response)
{
    errorReads = retryCount = 0;
    retryFile.clear();
    boxResult = response;
    _Book = new FailedBook(error, type);
    assert(OnOpenBookResult(nullptr, FALSE) == FALSE);
    assert(shownMessage == (error ? error : IDS_OPEN_FILE_FAILED));
    const bool retryable = type == book_online || type == book_web;
    assert(shownType == (retryable ? MB_RETRYCANCEL : MB_OK) + MB_ICONERROR);
    assert(retryCount == (response == IDRETRY ? 1 : 0));
    if (retryCount) assert(retryFile == L"C:\\books\\example.AZW3");
}
int main()
{
    check(230, book_mobi, 0);
    check(231, book_mobi, 0);
    check(232, book_mobi, 0);
    check(0, book_text, 0);
    check(0, book_online, IDRETRY);
    check(0, book_web, IDRETRY);
    check(0, book_online, 0);
}
'''
        with tempfile.TemporaryDirectory(prefix="reader-failure-") as temporary:
            path = Path(temporary)
            cpp = path / "failure.cpp"
            cpp.write_text(harness, encoding="utf-8")
            executable = path / "failure"
            subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic", str(cpp), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_extensions_dispatch_and_dialog_in_both_build_modes(self):
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("A C++ compiler is required for the portable routing checks")

        helper = re.search(
            r"static BOOL IsSupportedBookExtension\(const TCHAR \*ext\)\s*\{.*?\n\}",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(helper)
        mobi_helper = re.search(
            r"static BOOL IsMobiBookExtension\(const TCHAR \*ext\)\s*\{.*?\n\}",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(mobi_helper)
        self.assertIn("IsMobiBookExtension(ext)", helper.group(0))
        mobi_conditions = re.findall(
            r"else if \((IsMobiBookExtension\(ext\))\)\s*\{\s*_Book = new MobiBook;",
            SOURCE,
        )
        self.assertEqual(len(mobi_conditions), 1)
        dialog = re.search(
            r"#ifdef ENABLE_NETWORK\n\s*ofn\.lpstrFilter = .*?\n#endif",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(dialog)

        harness = r'''
#include <cassert>
#include <cwchar>
#include <cwctype>
#include <set>
#include <string>
using TCHAR = wchar_t;
using BOOL = bool;
#define FALSE false
#define _T(x) L##x
static int test_tcsicmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b && std::towlower(*a) == std::towlower(*b)) { ++a; ++b; }
    return static_cast<int>(std::towlower(*a)) - static_cast<int>(std::towlower(*b));
}
#define _tcsicmp test_tcsicmp
''' + mobi_helper.group(0) + "\n" + helper.group(0) + "\nstatic bool IsMobiExtension(const TCHAR *ext) { return " + mobi_conditions[0] + r'''; }
int main()
{
    for (const auto ext : {L".txt", L".TXT", L".epub", L".EPUB", L".mobi", L".MOBI", L".azw3", L".AZW3", L".AzW3", L".azw", L".AZW", L".AzW", L".prc", L".PRC", L".PrC", L".pdb", L".PDB", L".PdB"})
        assert(IsSupportedBookExtension(ext));
    for (const auto ext : {L"", L".", L".kfx", L".pdf", L".cbz", L".fb2", L".azw4", L".tpz", L".azw3.zip", L"azw3", L".azw3 "})
        assert(!IsSupportedBookExtension(ext));
    assert(!IsSupportedBookExtension(nullptr));
    assert(!IsMobiBookExtension(nullptr));
    for (const auto ext : {L".mobi", L".MOBI", L".azw3", L".AZW3", L".AzW3", L".azw", L".AZW", L".AzW", L".prc", L".PRC", L".PrC", L".pdb", L".PDB", L".PdB"})
        assert(IsMobiExtension(ext));
    for (const auto ext : {L".txt", L".epub", L".ol", L".web", L".kfx", L".azw4", L".tpz", L".azw3.zip", L""})
        assert(!IsMobiExtension(ext));
#ifdef ENABLE_NETWORK
    assert(IsSupportedBookExtension(L".OL"));
    assert(IsSupportedBookExtension(L".WEB"));
#else
    assert(!IsSupportedBookExtension(L".ol"));
    assert(!IsSupportedBookExtension(L".web"));
#endif
    struct { const wchar_t *lpstrFilter; } ofn;
''' + dialog.group(0) + r'''
    const wchar_t *pattern = ofn.lpstrFilter + std::wcslen(ofn.lpstrFilter) + 1;
    assert(pattern[std::wcslen(pattern) + 1] == L'\0');
    std::set<std::wstring> patterns;
    const std::wstring filter(pattern);
    for (size_t begin = 0; begin < filter.size(); ) {
        size_t end = filter.find(L';', begin);
        patterns.insert(filter.substr(begin, end - begin));
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    std::set<std::wstring> expected = {L"*.txt", L"*.epub", L"*.mobi", L"*.azw3", L"*.azw", L"*.prc", L"*.pdb"};
#ifdef ENABLE_NETWORK
    expected.insert(L"*.ol");
    expected.insert(L"*.web");
#endif
    assert(patterns == expected);
    assert(std::wstring(ofn.lpstrFilter) == L"Files (" + filter + L")");
}
'''
        with tempfile.TemporaryDirectory(prefix="reader-formats-") as temporary:
            path = Path(temporary)
            cpp = path / "formats.cpp"
            cpp.write_text(harness, encoding="utf-8")
            for network in (False, True):
                with self.subTest(network=network):
                    executable = path / ("formats-network" if network else "formats-offline")
                    command = compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic"]
                    if network:
                        command.append("-DENABLE_NETWORK")
                    subprocess.run(command + [str(cpp), "-o", str(executable)], check=True)
                    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
