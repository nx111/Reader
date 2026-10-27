"""Portable GDI boundary tests and source contracts; not a Windows GUI test."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
READER = ROOT / "Reader"


class PaintBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not shutil.which(compiler[0]):
            raise unittest.SkipTest("A C++ compiler is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="reader-paint-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        for name in ("GdiSurface.h", "ThemePalette.h", "ReaderInteraction.h"):
            shutil.copy2(READER / name, work / name)
        shutil.copy2(ROOT / "tests/reader_paint_compat.h", work / "framework.h")
        reader = (READER / "Reader.cpp").read_text()
        functions = [reader.split("static void SyncCurrentChapter(BOOL reveal)\n",1)[1].split("static void CloseToc",1)[0],
                     reader.split("LRESULT OnPaint(HWND hWnd, HDC hdc)\n",1)[1].split("VOID Invalidate",1)[0],
                     reader.split("VOID Invalidate(HWND hWnd, BOOL bOnlyClient, BOOL bErase)\n",1)[1].split("LRESULT OnSize",1)[0]]
        (work / "reader_paint.inc").write_text("static void SyncCurrentChapter(BOOL reveal)\n"+functions[0]
            +"LRESULT OnPaint(HWND hWnd, HDC hdc)\n"+functions[1]+"VOID Invalidate(HWND hWnd, BOOL bOnlyClient, BOOL bErase)\n"+functions[2]
            + "void Page::CalcPageDown(HDC hdc, RECT *rc)\n" + (READER / "Page.cpp").read_text().split("void Page::CalcPageDown(HDC hdc, RECT *rc)\n",1)[1].split("void Page::CalcPageUp",1)[0]
            + "void Page::CalcPageUp(HDC hdc, RECT *rc)\n" + (READER / "Page.cpp").read_text().split("void Page::CalcPageUp(HDC hdc, RECT *rc)\n",1)[1].split("int Page::GetMaxPageLength",1)[0])
        cls.binary = work / "reader-paint-test"
        flags = shlex.split(os.environ.get("READER_LIBRARY_TEST_FLAGS", ""))
        result = subprocess.run([*compiler,"-std=c++14","-Wall","-Wextra","-g",*flags,"-I"+str(work),
                                 str(ROOT / "tests/reader_paint_test.cpp"),"-o",str(cls.binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError(result.stdout)

    def run_case(self, name):
        result = subprocess.run([str(self.binary),name],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_repeated_dib_reuse_resize_failure_and_safe_deselection(self):
        self.run_case("reuse")

    def test_actual_paint_uses_child_local_bounds_and_preserves_anchor(self):
        self.run_case("paint")

    def test_actual_reading_invalidation_excludes_parent_and_siblings(self):
        self.run_case("dirty")

    def test_actual_pagination_keeps_anchor_when_reflow_cannot_fit_a_glyph(self):
        self.run_case("reflow")

    def test_actual_pagination_uses_inset_dimensions_once_in_both_directions(self):
        self.run_case("insets")

    def test_actual_chapter_sync_does_not_select_scroll_or_activate(self):
        self.run_case("sync")


class UiSourceContractTests(unittest.TestCase):
    def test_reading_insets_align_text_cover_pdf_and_editor_without_extra_margins(self):
        reader=(READER/"Reader.cpp").read_text()
        paint=reader.split("LRESULT OnPaint(HWND hWnd, HDC hdc)\n",1)[1].split("VOID Invalidate",1)[0]
        self.assertIn("ReaderReadingContentRect(page, GetWindowDpiCompat(_hReadingView), _header->internal_border)",paint)
        self.assertIn("IntersectClipRect(buffer, content.left, content.top, content.right, content.bottom)",paint)
        self.assertIn("DrawPage(hWnd, buffer, &content, FALSE)",paint)
        self.assertIn("DrawLoading(buffer, content)",paint)
        self.assertNotIn("InflateRect",paint)
        self.assertIn("rc.left + (rc.right - rc.left - width) / 2",reader)
        page=(READER/"Page.cpp").read_text()
        for coordinate in ("y = rc->top;","x = rc->left + p_line->x;","dst.X = rc->left + (w - bw)/2;","dst.Y = rc->top + (h - bh)/2;"):
            self.assertIn(coordinate,page)
        self.assertNotIn("m_header->internal_border",page)
        pdf=(READER/"PdfBook.cpp").read_text()
        self.assertIn("SetDIBitsToDevice(target,rect->left,rect->top,width,height",pdf)
        self.assertIn("rect->left+frame->left",pdf);self.assertIn("rect->top+frame->top",pdf)
        self.assertNotIn("InflateRect(&text",pdf)
        editor=(READER/"Editctrl.cpp").read_text()
        self.assertEqual(editor.count("    UpdateEditorContentRect();"),2)
        self.assertIn("ReaderReadingContentRect(client, GetWindowDpiCompat(g_hEditCtrl), _header->internal_border)",editor)
        self.assertIn("EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0",editor)
        self.assertIn("sidebar&&!compact?ReaderSidebarPadding(dpi):",(READER/"Theme.cpp").read_text())

    def test_scrollbars_region_routing_full_labels_and_splitter_cancel(self):
        reader = (READER / "Reader.cpp").read_text()
        self.assertNotIn("TVS_NOHSCROLL",reader)
        self.assertEqual(reader.count("TVS_INFOTIP"),2)
        self.assertEqual(reader.count("WS_HSCROLL | WS_VSCROLL"),2)
        toc = reader.split("static LRESULT CALLBACK TocProc(HWND window,",1)[1].split("static BOOL ReaderShortcut",1)[0]
        for contract in ("WM_MOUSEHWHEEL", "WM_HSCROLL", "MK_SHIFT", "!PtInRect", "_markProc", "CopyTreeLabel"):
            self.assertIn(contract,toc)
        wheel = reader.split("case WM_MOUSEHWHEEL:",2)[2].split("case WM_ERASEBKGND:",1)[0]
        self.assertIn("PtInRect(&_readingRect",wheel)
        self.assertNotIn("SetFocus",wheel)
        splitter = reader.split("static LRESULT CALLBACK SplitterProc(HWND window,",1)[1].split("void ApplyReaderTheme",1)[0]
        for contract in ("IDC_SIZEWE", "SetCapture", "ReleaseCapture", "WM_CANCELMODE", "WM_CAPTURECHANGED", "WM_KILLFOCUS", "VK_ESCAPE", "VK_HOME", "DLGC_WANTARROWS"):
            self.assertIn(contract,splitter)
        drag = splitter.split("case WM_MOUSEMOVE:",1)[1].split("case WM_LBUTTONUP:",1)[0]
        self.assertIn("_sidebarPreviewWidth = width",drag)
        self.assertNotIn("_header->sidebar_width =",drag)
        cancel = reader.split("static void CancelSplitter(HWND owner)",1)[1].split("static LRESULT CALLBACK SplitterProc",1)[0]
        self.assertLess(cancel.index("_splitterDrag.Cancel()"),cancel.index("ReleaseCapture()"))
        self.assertNotIn("Save(",cancel)

    def test_jump_scroll_and_opaque_render_call_chains(self):
        reader = (READER / "Reader.cpp").read_text()
        page = (READER / "Page.cpp").read_text()
        repaint = page.split("void Page::ReDraw(HWND hWnd)",1)[1].split("int Page::GetPageLength",1)[0]
        self.assertIn("Invalidate(hWnd, TRUE, FALSE)",repaint)
        parent = reader.split("LRESULT CALLBACK WndProc",1)[1].split("case WM_PAINT:",1)[1].split("case WM_CLOSE:",1)[0]
        self.assertIn("ExcludeClipRect",parent)
        self.assertNotIn("OnPaint(",parent)
        menu = reader.split("LRESULT OnUpdateMenu(HWND hWnd)\n",1)[1].split("LRESULT OnOpenItem",1)[0]
        self.assertIn("ThemeUpdateMenu",menu)
        self.assertNotIn("ThemeAttachWindow",menu)
        self.assertIn("WS_CLIPCHILDREN",reader)
        self.assertNotIn("WS_EX_COMPOSITED",reader)
        self.assertNotIn("UpdateLayeredWindow",reader)
        self.assertNotIn("SetLayeredWindowAttributes",reader)
        self.assertNotIn("ResetLayerd",reader)
        self.assertIn("m_AlphaSurface.Ensure",page)
        self.assertNotIn("DeleteAlphaTextBitmap",page)
        for name in ("Chapters", "BookMark"):
            fn = reader.split("LRESULT OnUpdate"+name+"(HWND hWnd)\n",1)[1].split("\nLRESULT ",1)[0]
            self.assertLess(fn.index("return 0;"),fn.index("TreeView_DeleteAllItems"))

    def test_theme_coverage_settings_cancel_and_native_boundaries(self):
        theme = (READER / "Theme.cpp").read_text()
        for contract in ("WM_CTLCOLORDLG","WM_CTLCOLORSTATIC","WM_CTLCOLOREDIT","WM_CTLCOLORLISTBOX","WM_CTLCOLORBTN","WC_TREEVIEW","WC_LISTVIEW","WC_HEADER","STATUSCLASSNAME","TOOLTIPS_CLASS","PROGRESS_CLASS","MSAAMENUINFO","MSAA_MENU_SIG","WM_MENUCHAR","DrawFocusRect","HCF_HIGHCONTRASTON"):
            self.assertIn(contract,theme)
        self.assertNotIn("GetProcAddress",theme)
        for name in ("Reader","DisplaySet","Advset","Keyset","OnlineDlg","WebDlg","BooksourceDlg","tagset","LibraryDlg","SearchDlg"):
            self.assertIn("ThemeAttachWindow", (READER/(name+".cpp")).read_text(encoding="utf-8-sig"))
        display = (READER / "DisplaySet.cpp").read_text()
        self.assertIn("if (_display.is_save)",display)
        self.assertIn("ApplyReaderTheme()",display)
        cancel = display.split("case IDCANCEL:",1)[1].split("break;",1)[0]
        self.assertNotIn("ApplyReaderTheme",cancel)
        self.assertNotIn("_header->",cancel)
        # Font-dialog hooks and the system file/color dialogs retain their OS UI.
        self.assertNotIn("ThemeAttachWindow",display.split("case WM_INITDIALOG:",2)[2])
        rc = (READER / "Reader_zh-cn.rc").read_text(encoding="utf-16")
        for control in ("IDC_COMBO_THEME", "IDC_CHECK_CUSTOM_READING"):
            self.assertIn(control,rc)
        for name in ("Page","SearchDlg"):
            source=(READER/(name+".cpp")).read_text()
            self.assertIn("ThemeColors().hit",source)
        self.assertIn("ThemeReadingColors",(READER/"Page.cpp").read_text())

    def test_node24_workflow_preserves_build_package_inputs_and_project_registration(self):
        workflow = ROOT / ".github/workflows/build-package.yml"
        old = subprocess.check_output(["git","show","HEAD:.github/workflows/build-package.yml"],cwd=ROOT,text=True)
        expected = old.replace("actions/checkout@v4","actions/checkout@v7").replace("actions/upload-artifact@v4","actions/upload-artifact@v7").replace("microsoft/setup-msbuild@v2","microsoft/setup-msbuild@v3")
        actual = workflow.read_text()
        for action in ("actions/checkout@v7", "actions/upload-artifact@v7", "microsoft/setup-msbuild@v3"):
            self.assertIn(action, actual)
        for argument in ("/p:Configuration=Release", "/p:PlatformToolset=v143", "/p:WindowsTargetPlatformVersion=10.0"):
            self.assertIn(argument, actual)
        self.assertIn("scripts/fetch-pdfium.ps1", actual)
        self.assertIn("tests/run_windows_pdf_smoke.ps1", actual)
        self.assertIn("tests/verify_windows_package.ps1", actual)
        self.assertIn("platform: x86", actual); self.assertIn("platform: x64", actual)
        solution = (ROOT / "Reader.sln").read_text(encoding="utf-8-sig")
        for platform in ("x86", "x64"):
            self.assertIn("Release|" + platform + " = Release|" + platform, solution)
        self.assertIn("fail-fast: false", actual)
        self.assertIn("pdfium.dll", actual); self.assertIn("opensrc/pdfium/licenses", actual)
        self.assertNotIn("permissions:", actual); self.assertNotIn("secrets.", actual)
        self.assertNotIn("ACTIONS_ALLOW_USE_UNSECURE_NODE_VERSION", actual)
        ns={"m":"http://schemas.microsoft.com/developer/msbuild/2003"}
        for project in ("Reader.vcxproj","Reader.vcxproj.filters"):
            root=ET.parse(READER/project).getroot()
            for kind,names in (("ClCompile",["Theme.cpp"]),("ClInclude",["Theme.h","ThemePalette.h","GdiSurface.h"])):
                includes=[x.attrib["Include"] for x in root.findall(".//m:"+kind,ns) if "Include" in x.attrib]
                for name in names:
                    self.assertEqual(includes.count(name),1)


if __name__ == "__main__":
    unittest.main()
