"""Optional MinGW syntax check, not a Windows/MSVC link or GUI test.

No downloads. Pass --compiler for a locally available x86_64 MinGW g++.
Production source is copied unchanged except lossless UTF-16 resource-header
conversion. Temporary case aliases and an MSVC max() compatibility function
cover differences in the Windows headers shipped by MinGW on Linux.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="x86_64-w64-mingw32-g++")
    parser.add_argument("--windows-include", type=Path)
    parser.add_argument("--network", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("MinGW g++ is unavailable; a Windows/MSVC build is still required")
    windows = args.windows_include
    if windows is None:
        library = subprocess.check_output([compiler, "-print-file-name=libkernel32.a"], text=True).strip()
        windows = Path(library).resolve().parent.parent / "include"
    if not (windows / "windows.h").exists():
        parser.error("Cannot find the MinGW Windows SDK headers; pass --windows-include")
    with tempfile.TemporaryDirectory(prefix="reader-windows-syntax-") as directory:
        work = Path(directory)
        source, aliases = work / "Reader", work / "include"
        source.mkdir()
        aliases.mkdir()
        for file in (ROOT / "Reader").iterdir():
            if file.suffix in (".cpp", ".h"):
                raw = file.read_bytes()
                (source / file.name).write_bytes(
                    raw.decode("utf-16").encode("utf-8") if raw.startswith(b"\xff\xfe") else raw)
        for file in source.iterdir():
            for name in re.findall(r'#include\s+[<"]([^>"]+)', file.read_text(encoding="utf-8")):
                if "/" not in name and not (windows / name).exists() and (windows / name.lower()).exists():
                    if not (aliases / name).exists():
                        (aliases / name).symlink_to(windows / name.lower())
        compatibility = work / "msvc_compat.h"
        compatibility.write_text(
            "#include <algorithm>\n"
            "template<class T,class U> auto max(T a,U b) -> decltype(a > b ? a : b) "
            "{ return a > b ? a : b; }\n")
        flags = ["-std=gnu++14", "-fsyntax-only", "-fpermissive", "-DUNICODE", "-D_UNICODE",
                 "-D_M_X64", "-D_WIN32_WINNT=0x0601", "-D_CRT_SECURE_NO_WARNINGS",
                 "-DLIBXML_STATIC", "-DLIBHTTPS_STATIC", "-DCJSON_HIDE_SYMBOLS", "-DZLIB_WINAPI",
                 "-include", str(compatibility), "-I" + str(aliases), "-I" + str(source)]
        for name in ("cjson", "zlib/inc", "libxml2/inc", "libhttps/inc", "miniz", "libmobi/inc"):
            flags.append("-I" + str(ROOT / "opensrc" / name))
        if args.network:
            flags.append("-DENABLE_NETWORK")
        failed = False
        for name in ("Reader", "Book", "Cache", "Jsondata", "LibraryDlg", "SearchDlg", "Page", "WebBook", "Editctrl",
                     "Advset", "DPIAwareness", "EpubBook", "HtmlParser", "Theme", "DisplaySet", "Keyset", "OnlineDlg", "WebDlg", "BooksourceDlg", "tagset"):
            result = subprocess.run([compiler, *flags, str(source / (name + ".cpp"))],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(name + ": " + ("FAIL" if result.returncode else "PASS"), flush=True)
            if result.returncode:
                print(result.stdout)
                failed = True
        return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
