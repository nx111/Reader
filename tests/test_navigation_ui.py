"""Final left-navigation layout, keyboard and source-boundary regressions."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]


class NavigationLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="reader-navigation-");cls.addClassCleanup(cls.temp.cleanup)
        cls.binary=Path(cls.temp.name)/"navigation-test"
        result=subprocess.run([*shlex.split(os.environ.get("CXX","g++")),"-std=c++14","-Wall","-Wextra","-g",
                               *shlex.split(os.environ.get("READER_LIBRARY_TEST_FLAGS","")),"-I"+str(ROOT/"Reader"),
                               str(ROOT/"tests/navigation_ui_test.cpp"),"-o",str(cls.binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode:raise AssertionError(result.stdout)

    def run_case(self,case):
        result=subprocess.run([str(self.binary),case],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_left_navigation_no_top_row_and_exclusive_drawer_dpi_geometry(self):self.run_case("layout")
    def test_navigation_scrolls_without_covering_header_or_history(self):self.run_case("scroll")
    def test_docked_library_has_one_navigation_and_wraps_icons_on_narrow_windows(self):self.run_case("library")
    def test_alt_f10_arrow_activation_repeat_escape_and_altgr(self):self.run_case("keys")
    def test_all_monochrome_vectors_have_bounded_nonempty_strokes(self):self.run_case("icons")
    def test_light_dark_selected_hover_pressed_disabled_palette(self):self.run_case("colors")
    def test_pdf_fit_zoom_scroll_and_resize_preserve_normalized_anchor(self):self.run_case("pdf_geometry")
    def test_pdf_history_preserves_same_page_viewport_jumps(self):self.run_case("pdf_history")
    def test_bookmark_export_unicode_notes_markdown_escape_and_empty_book(self):self.run_case("export")

    def test_custom_shortcuts_precede_navigation_without_changing_defaults(self):
        function=(ROOT/'Reader/Keyset.cpp').read_text().split('BOOL KS_CustomKeyDownProc(',1)[1].split('BOOL KS_KeyDownProc(',1)[0]
        source=r'''
#include <cassert>
using DWORD=unsigned;using UINT=unsigned;using WPARAM=unsigned;using LPARAM=long;using HWND=void*;using BOOL=bool;
#define TRUE true
#define FALSE false
#define LOWORD(x) ((x)&65535)
enum {KI_HIDE=0,KI_MAXCOUNT=3,KT_SHORTCUTKEY=1};
int calls=0;unsigned values[]={84,119,37};int disabled[]={0,0,1};
void Run(HWND,UINT,WPARAM,LPARAM) {++calls;}
struct Key {unsigned key,*pvalue;int* is_disable;unsigned defval;void (*proc)(HWND,UINT,WPARAM,LPARAM);};
Key g_Keysets[]={{1,&values[0],&disabled[0],84,Run},{1,&values[1],&disabled[1],99,Run},{1,&values[2],&disabled[2],100,Run}};
DWORD ToHotkey(WPARAM value) {return value;}
BOOL KS_CustomKeyDownProc('''+function+r'''
int main() {
 assert(!KS_CustomKeyDownProc(nullptr,0,84,0)&&calls==0);
 assert(KS_CustomKeyDownProc(nullptr,0,119,0)&&calls==1);
 assert(KS_CustomKeyDownProc(nullptr,0,37,0)&&calls==1);
 assert(!KS_CustomKeyDownProc(nullptr,0,120,0)&&calls==1);
}
'''
        cpp=Path(self.temp.name)/'custom-shortcuts.cpp';exe=cpp.with_suffix('');cpp.write_text(source)
        result=subprocess.run([*shlex.split(os.environ.get('CXX','g++')),'-std=c++14',*shlex.split(os.environ.get('READER_LIBRARY_TEST_FLAGS','')),str(cpp),'-o',str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)
        result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(result.returncode,0,result.stdout)


class NavigationSourceTests(unittest.TestCase):
    def test_native_buttons_accessibility_popup_ownership_and_keyboard_focus(self):
        source=(ROOT/"Reader/NavigationPane.cpp").read_text()
        for feature in ('L"BUTTON"','WS_TABSTOP','TTM_ADDTOOL','ReaderCompact','SetFocus(buttons[6])','EnsureFocusVisible','CancelReaderNavigation','TrackPopupMenuEx','RemoveMenu(popup,0,MF_BYPOSITION)','previousFocus','GetForegroundWindow'):
            self.assertIn(feature,source)
        self.assertLess(source.index('if(command) PostMessage(owner,WM_COMMAND'),source.index('PostMessage(owner,WM_READER_NATIVE_MENU_CLOSED'))
        self.assertIn('SetMenu(owner,NULL)',source)
        self.assertIn('ReaderNavigationTracking() {return tracking || finishing;}',source)
        root=(ROOT/'Reader/Reader.cpp').read_text()
        closed=root.split('case WM_READER_NATIVE_MENU_CLOSED:',1)[1].split('case ',1)[0]
        self.assertLess(closed.index('ReaderNavigationMenuClosed()'),closed.index('OnUpdateMenu'))
        for forbidden in ('TOOLBARCLASSNAME','WH_MSGFILTER','WM_NCPAINT','DrawMenuBar','SetWindowsHookEx'):
            self.assertNotIn(forbidden,source)

    def test_library_panel_and_body_do_not_overlap_and_wheel_routes_by_region(self):
        source=(ROOT/"Reader/Reader.cpp").read_text()
        layout=source.split('static void UpdateReaderLayout(HWND hWnd)\n',1)[1].split('static void SyncCurrentChapter',1)[0]
        self.assertIn('LayoutLibraryView(hWnd,&reading',layout);self.assertIn('PositionControl(_hReadingView,HWND_BOTTOM,reading.left,0',layout)
        self.assertIn('EC_UpdateReadingRect(&reading)',layout);self.assertNotIn('toolbarHeight',layout)
        self.assertIn('ReaderNavigationWheel(message,wParam,lParam)',source)
        self.assertIn('PtInRect(&_readingRect, wheel) || _libraryVisible',source)
        library=(ROOT/'Reader/LibraryDlg.cpp').read_text()
        self.assertIn('WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS',library.replace(' ',''))
        self.assertIn('ReaderLibraryLayout(rc.right,rc.bottom,libraryDpi,false)',library.replace(' ',''))

    def test_bookmark_edit_cancel_stable_identity_and_atomic_export(self):
        source=(ROOT/'Reader/Reader.cpp').read_text();actions=(ROOT/'Reader/BookmarkActions.cpp').read_text()
        self.assertIn('_Cache.get_by_id(id)',source);self.assertIn('_Book->GetOpenToken() == token',source)
        self.assertIn('if (!EditReaderBookmarkNote(owner,detail.note)) return',source)
        self.assertIn('OFN_OVERWRITEPROMPT',actions);self.assertIn('FlushFileBuffers',actions)
        self.assertIn('MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH',actions)
        self.assertLess(actions.index('if (LOWORD(wParam) == IDOK)'),actions.index('*note = text'))
        # Exercise the production export operation against a fault-injected
        # filesystem. This checks cancellation and preserving existing output;
        # the OS save-dialog UI and NTFS guarantees still require Windows.
        function=actions.split('BOOL ExportReaderBookmarks(',1)[1]
        harness=r'''
#include "library_compat.h"
#include "BookmarkExport.h"
std::map<std::wstring,TestFile> TestFiles;std::vector<std::string> TestOperations;std::string TestFailure;
#define _wcsicmp _tcsicmp
enum {OFN_OVERWRITEPROMPT=1,OFN_PATHMUSTEXIST=2,OFN_NOCHANGEDIR=4,MB_OK=0,MB_ICONERROR=16};
struct OPENFILENAME {unsigned lStructSize;HWND hwndOwner;const wchar_t* lpstrFilter;wchar_t* lpstrFile;unsigned nMaxFile;const wchar_t* lpstrDefExt;unsigned Flags;};
bool cancel=false;int errors=0;const wchar_t* target=L"C:\\Reader\\export.md";const wchar_t* temporary=L"C:\\Reader\\Rbm.tmp";
bool GetSaveFileName(OPENFILENAME* p) {assert(p->Flags&OFN_OVERWRITEPROMPT);if(cancel)return false;wcscpy(p->lpstrFile,target);return true;}
const wchar_t* PathFindExtension(const wchar_t* p) {auto dot=wcsrchr(p,L'.');return dot?dot:p+wcslen(p);}
void PathRemoveFileSpec(wchar_t* p) {auto slash=wcsrchr(p,L'\\');if(slash)*slash=0;}
int GetTempFileName(const wchar_t*,const wchar_t*,int,wchar_t* p) {if(TestFail("temp"))return 0;wcscpy(p,temporary);TestFiles[p];return 1;}
BOOL WriteFile(HANDLE f,const void* p,DWORD n,DWORD* out,void* overlapped) {return WriteFile(f,const_cast<void*>(p),n,out,overlapped);}
int MessageBox(HWND,const wchar_t*,const wchar_t*,int) {++errors;return 0;}
std::string Utf16ToUtf8(const wchar_t* p) {return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(p);}
BOOL ExportReaderBookmarks('''+function+r'''
int main() {
 bookmark_detail_t mark;mark.title=L"中文书签";mark.note=L"我的备注";
 for(const char* fault:{"temp","create","write","write_short","flush","replace"}) {
  TestFiles.clear();TestOperations.clear();TestFailure=fault;errors=0;TestFiles[target].bytes={'o','l','d'};
  assert(!ExportReaderBookmarks(nullptr,L"Book",{mark}));assert(errors==1);
  assert(std::string(TestFiles[target].bytes.begin(),TestFiles[target].bytes.end())=="old");assert(!TestFiles.count(temporary));
 }
 TestFailure.clear();TestOperations.clear();errors=0;cancel=true;
 assert(!ExportReaderBookmarks(nullptr,L"Book",{mark})&&TestOperations.empty()&&errors==0);
 cancel=false;assert(ExportReaderBookmarks(nullptr,L"Book",{mark})&&errors==0&&!TestFiles.count(temporary));
 auto first=TestFiles[target].bytes;assert(ExportReaderBookmarks(nullptr,L"Book",{mark})&&TestFiles[target].bytes==first);
 assert(std::string(first.begin(),first.end()).find(Utf16ToUtf8(L"我的备注"))!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory(prefix='reader-export-') as folder:
            cpp=Path(folder)/'export.cpp';exe=cpp.with_suffix('');cpp.write_text(harness)
            result=subprocess.run([*shlex.split(os.environ.get('CXX','g++')),'-std=c++14',*shlex.split(os.environ.get('READER_LIBRARY_TEST_FLAGS','')),'-I'+str(ROOT/'Reader'),'-I'+str(ROOT/'tests'),str(cpp),'-o',str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode,0,result.stdout)
            result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(result.returncode,0,result.stdout)

    def test_pdf_lazy_worker_is_bounded_cancelled_and_app_local(self):
        book=(ROOT/'Reader/PdfBook.cpp').read_text();engine=(ROOT/'Reader/PdfEngine.cpp').read_text()
        for feature in ('stopping=true; ++revision','worker.join()','revision!=current','it-cache.begin()>=2','64*1024*1024','WM_READER_PDF_FRAME'):
            self.assertIn(feature,book)
        self.assertNotIn('TerminateThread',book);self.assertNotIn('document.Size(',book.split('void PdfBook::DrawPage',1)[1])
        self.assertIn('LOAD_WITH_ALTERED_SEARCH_PATH',engine);self.assertIn('std::lock_guard<std::mutex> lock(engineLock)',engine)
        self.assertIn('request == expected',engine);self.assertIn('FPDF_RenderPage_Continue',engine)
        self.assertNotIn('ShellExecute',engine);self.assertNotIn('FPDFDOC_InitFormFillEnvironment',engine)

    def test_pdf_scope_fixed_layout_editor_and_legacy_shortcuts(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text();keys=(ROOT/'Reader/Keyset.cpp').read_text()
        self.assertIn('PDF 当前页文本',reader);self.assertIn('不含扫描图片 OCR',reader)
        self.assertIn('PDF 为固定页面',reader)
        for command in ('OnFontZoomIn','OnFontZoomOut'):
            self.assertIn(command,keys);self.assertIn('((PdfBook*)_Book)->Zoom',reader.split('LRESULT '+command+'(',1)[1].split('\nLRESULT ',1)[0])
        self.assertIn('WM_READER_PDF_FRAME',reader);self.assertIn('ReaderOpenResultIsCurrent(_Book->GetOpenToken(),(UINT)lParam)',reader)
