"""Focus Escape production bridge, layout/palette math and padding contracts.
No real Windows GUI, DPI pixels or screen reader execution is claimed.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class FocusUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='reader-focus-');cls.addClassCleanup(cls.temp.cleanup)
        work=Path(cls.temp.name);source=(ROOT/'Reader/Reader.cpp').read_text()
        actual=source.split('static void SetReaderFocusMode(BOOL focus)\n',1)[1].split('static BOOL ReaderShortcut(MSG* message)',1)[0]
        (work/'reader_focus.inc').write_text('static void SetReaderFocusMode(BOOL focus)\n'+actual)
        cls.exe=work/'focus-test'
        result=subprocess.run([*shlex.split(os.environ.get('CXX','g++')),'-std=c++14','-Wall','-Wextra',*shlex.split(os.environ.get('READER_LIBRARY_TEST_FLAGS','')),'-I'+str(work),'-I'+str(ROOT/'Reader'),str(ROOT/'tests/focus_ui_test.cpp'),'-o',str(cls.exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode:raise AssertionError(result.stdout)

    def run_case(self,case):
        result=subprocess.run([str(self.exe),case],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_actual_escape_priorities_repeat_key_release_loading_and_foreign_windows(self):self.run_case('escape')
    def test_actual_repeated_focus_restore_preserves_toc_bookmarks_and_anchor(self):self.run_case('restore')
    def test_permanent_compact_rail_reserves_space_at_100_to_200_dpi(self):self.run_case('geometry')
    def test_mouse_focus_menu_capture_and_app_activation_choose_clear_rail(self):self.run_case('emphasis')
    def test_local_blend_preserves_readable_light_dark_colors(self):self.run_case('palette')
    def test_shared_internal_padding_scales_and_clamps_on_narrow_columns(self):self.run_case('padding')

    def test_event_routes_have_no_hover_hiding_and_only_repaint_the_rail(self):
        nav=(ROOT/'Reader/NavigationPane.cpp').read_text();reader=(ROOT/'Reader/Reader.cpp').read_text()
        self.assertNotIn('SetTimer',nav);self.assertNotIn('SetLayeredWindowAttributes',nav)
        emphasis=nav.split('void RefreshEmphasis(',1)[1].split('void Position(',1)[0]
        self.assertIn('RedrawWindow(pane',emphasis);self.assertNotIn('UpdateReaderLayout',emphasis);self.assertNotIn('Invalidate(owner',emphasis)
        self.assertIn('L"退出专注，固定侧栏（Esc）"',nav);self.assertIn('ShowWindow(focusExit,focusMode?SW_SHOWNA:SW_HIDE)',nav)
        access=reader.split('case WM_READER_NAVIGATION_ACCESS:',1)[1].split('case ',1)[0]
        self.assertNotIn('UpdateReaderLayout',access);self.assertNotIn('Invalidate',access)
        loop=reader.split('// Main message loop:',1)[1].split('    Exit();',1)[0]
        self.assertLess(loop.index('ReaderSearchTranslate'),loop.index('ReaderFocusEscape'));self.assertLess(loop.index('ReaderFocusEscape'),loop.index('KS_CustomKeyDownProc'))
        hook=reader.split('LRESULT CALLBACK KeyboardProc(',1)[1]
        self.assertLess(hook.index('ReaderFocusEscape'),hook.index('KS_KeyDownProc'))

    def test_padding_uses_native_gutter_and_cell_geometry_without_fake_text_spaces(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text();theme=(ROOT/'Reader/Theme.cpp').read_text();search=(ROOT/'Reader/SearchDlg.cpp').read_text()
        gutter=reader.split('static void UpdateTreePadding()',1)[1].split('void SetTreeviewFont()',1)[0]
        for feature in ('ReaderItemPadding','TreeView_SetImageList','ImageList_AddMasked','ImageList_Destroy(old)'):self.assertIn(feature,gutter)
        self.assertNotIn('TVM_SETBORDER',reader);self.assertNotIn('TreeView_SetIndent',gutter)
        self.assertIn('ListView_GetSubItemRect',theme);self.assertIn('Header_GetItemRect',theme)
        self.assertIn('ReaderItemInset(cell.right-cell.left',theme);self.assertIn('ReaderItemInset(rc.right-rc.left',search)
        self.assertIn('if(inherited&CDRF_SKIPDEFAULT) return inherited',theme)
        self.assertIn('ReaderPaddedList',(ROOT/'Reader/LibraryDlg.cpp').read_text());self.assertIn('ReaderPaddedList',search)
