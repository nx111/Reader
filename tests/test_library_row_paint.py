"""Compile the production library-row/cell painters with GDI/list-view substitutes."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class LibraryRowPaintTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="reader-library-row-")
        cls.addClassCleanup(cls.temp.cleanup)
        work=Path(cls.temp.name)
        for name in ("ThemePalette.h","ReaderInteraction.h"):
            shutil.copy2(ROOT/"Reader"/name,work/name)
        source=(ROOT/"Reader/Theme.cpp").read_text()
        painters="LRESULT DrawPaddedCell("+source.split("LRESULT DrawPaddedCell(",1)[1].split("void RestoreMenu",1)[0]
        (work/"library_row_paint.inc").write_text(painters)
        cls.binary=work/"library-row-test"
        result=subprocess.run([*shlex.split(os.environ.get("CXX","g++")),"-std=c++14","-Wall","-Wextra",
                               "-I"+str(work),str(ROOT/"tests/library_row_paint_test.cpp"),"-o",str(cls.binary)],
                              text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode:raise AssertionError(result.stdout)

    def run_case(self,name):
        result=subprocess.run([str(self.binary),name],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_continuous_selection_single_focus_and_cell_padding_with_dpi_scroll_and_wide_client(self):self.run_case("geometry")
    def test_native_multi_selection_focus_visibility_disabled_and_high_contrast_colors(self):self.run_case("states")
    def test_other_padded_lists_keep_their_draw_route_and_invalid_rows_fall_back(self):self.run_case("scope")

    def test_library_marker_routes_whole_item_before_subitem_draw_without_state_mutation(self):
        theme=(ROOT/"Reader/Theme.cpp").read_text()
        dispatch=theme.split('if (!_wcsicmp(type, WC_TREEVIEW) || !_wcsicmp(type, WC_LISTVIEW))',1)[1].split('bool button =',1)[0]
        self.assertIn('draw->dwDrawStage==CDDS_ITEMPREPAINT && GetProp(from,L"ReaderLibraryList")',dispatch)
        self.assertLess(dispatch.index('DrawLibraryRow('),dispatch.index('DrawPaddedCell('))
        row=theme.split('LRESULT DrawLibraryRow(',1)[1].split('void RestoreMenu',1)[0]
        self.assertIn('CDRF_SKIPDEFAULT|CDRF_SKIPPOSTPAINT',row)
        for mutation in ('ListView_SetItemState','SetFocus(','InvalidateRect','UpdateWindow','RedrawWindow'):
            self.assertNotIn(mutation,row)
        library=(ROOT/"Reader/LibraryDlg.cpp").read_text()
        self.assertIn('LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER',library)
        self.assertIn('SetProp(list,L"ReaderLibraryList",(HANDLE)1)',library)


if __name__=="__main__":unittest.main()
