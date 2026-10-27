"""Native title-bar API bridge only; not a Windows desktop visual test."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]


class TitleBarTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="reader-titlebar-")
        cls.addClassCleanup(cls.temp.cleanup)
        work=Path(cls.temp.name)
        for name in ("TitleBarTheme.h","TitleBarTheme.cpp","ThemePalette.h"):
            shutil.copy2(ROOT/"Reader"/name,work/name)
        shutil.copy2(ROOT/"tests/titlebar_compat.h",work/"framework.h")
        cls.binary=work/"titlebar-test"
        result=subprocess.run([*shlex.split(os.environ.get("CXX","g++")),"-std=c++14","-Wall","-Wextra",
                               "-I"+str(work),str(work/"TitleBarTheme.cpp"),str(ROOT/"tests/titlebar_test.cpp"),
                               "-o",str(cls.binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode:raise AssertionError(result.stdout)

    def run_case(self,name):
        result=subprocess.run([str(self.binary),name],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_light_dark_caption_text_and_repeated_mode_changes(self):self.run_case("colors")
    def test_high_contrast_and_query_failure_restore_system_defaults(self):self.run_case("contrast")
    def test_rejected_attributes_rollback_both_colors_and_leave_native_fallback(self):self.run_case("fallback")
    def test_missing_system_library_or_export_is_safe_and_never_loads_from_app_directory(self):
        for case in ("directory_error","directory_long","missing_library","missing_export"):
            with self.subTest(case=case):self.run_case(case)

    def test_startup_theme_and_system_change_wiring_preserves_native_opaque_window(self):
        theme=(ROOT/"Reader/Theme.cpp").read_text()
        self.assertIn("if (data->root) { PrepareWindowMenu(window); ApplyTitleBar(window); }",theme)
        self.assertIn("ApplyNativeTitleBarTheme(window, MakeReaderPalette(currentTheme), currentTheme != 0)",theme)
        configure=theme.split("void ThemeConfigure(const header_t* settings)",1)[1].split("void ThemeAttachWindow",1)[0]
        self.assertLess(configure.index("currentTheme = settings->ui_theme"),configure.index("ThemeRefreshAll()"))
        route=theme.split("WindowData* data = (WindowData*)reference;",1)[1].split("if (message == WM_NCDESTROY)",1)[0]
        for message in ("WM_THEMECHANGED","WM_SETTINGCHANGE","WM_SYSCOLORCHANGE","WM_DWMCOMPOSITIONCHANGED","WM_STYLECHANGED"):
            self.assertIn(message,route)
        self.assertIn("ApplyTitleBar(window)",route)
        self.assertIn("data->root",route)
        native=(ROOT/"Reader/TitleBarTheme.cpp").read_text()
        for forbidden in ("SetWindowLong", "SetWindowPos", "SetLayeredWindowAttributes", "UpdateLayeredWindow",
                          "DwmExtendFrameIntoClientArea", "MAKEINTRESOURCE", "InvalidateRect", "RedrawWindow"):
            self.assertNotIn(forbidden,native)
        ns={"m":"http://schemas.microsoft.com/developer/msbuild/2003"}
        for project in ("Reader.vcxproj","Reader.vcxproj.filters"):
            root=ET.parse(ROOT/"Reader"/project).getroot()
            for kind,name in (("ClCompile","TitleBarTheme.cpp"),("ClInclude","TitleBarTheme.h")):
                self.assertEqual(sum(item.get("Include")==name for item in root.findall(".//m:"+kind,ns)),1)


if __name__=="__main__":unittest.main()
