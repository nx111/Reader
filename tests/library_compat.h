#pragma once
// Portable Win32 stand-ins for exercising production Cache/Jsondata logic.
// File operations use a fault-injectable in-memory filesystem; no GUI/GDI claim.
#include <algorithm>
#include <cassert>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <locale>
#include <map>
#include <string>
#include <vector>

using VOID = void; using LPCTSTR = const wchar_t*;
using BOOL = int; using BYTE = unsigned char; using TCHAR = wchar_t; using WCHAR = wchar_t;
using DWORD = uint32_t; using UINT = unsigned; using LONG = int32_t;
using HWND = void*; using HANDLE = void*; using HFONT = void*;
using HDC = void*; using HMENU = void*; using WPARAM = uintptr_t; using LPARAM = intptr_t;
using LRESULT = intptr_t; using INT_PTR = intptr_t;
#define CALLBACK
#define __stdcall
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define LF_FACESIZE 32
#define _T(s) L##s
#define _tcscpy wcscpy
#define _tcsncpy wcsncpy
#define _tcsrchr wcsrchr
#define _tcslen wcslen
#define _tcscmp wcscmp
inline int _tcsicmp(const wchar_t* a, const wchar_t* b)
{
    while (*a && *b && towlower(*a) == towlower(*b)) { ++a; ++b; }
    return towlower(*a) < towlower(*b) ? -1 : towlower(*a) > towlower(*b) ? 1 : 0;
}
// MSVC's wide printf uses %s for wide strings.
template<class... Args> int _stprintf(wchar_t* buffer, const wchar_t* format, Args... args)
{
    std::wstring f(format);
    size_t at = 0;
    while ((at = f.find(L"%s", at)) != std::wstring::npos) { f.replace(at, 2, L"%ls"); at += 3; }
    return swprintf(buffer, 512, f.c_str(), args...);
}
struct POINT { LONG x, y; };
struct RECT { LONG left, top, right, bottom; };
struct WINDOWPLACEMENT { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; };
struct LOGFONT
{
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
};
struct LARGE_INTEGER { long long QuadPart; };
struct BY_HANDLE_FILE_INFORMATION { DWORD dwVolumeSerialNumber, nFileIndexHigh, nFileIndexLow; };

#define WM_USER 1024
#define WS_OVERLAPPEDWINDOW 0x00cf0000
#define WS_EX_ACCEPTFILES 16
#define WS_EX_LAYERED 0x80000
#define SW_SHOWNORMAL 1
#define FW_LIGHT 300
#define ANSI_CHARSET 0
#define OUT_STROKE_PRECIS 3
#define CLIP_STROKE_PRECIS 2
#define PROOF_QUALITY 2
#define VARIABLE_PITCH 2
#define FF_SWISS 32
#define LOGPIXELSY 90
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define FILE_READ_ATTRIBUTES 4
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 128
#define FILE_ATTRIBUTE_HIDDEN 2
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

struct TestFile { std::vector<char> bytes; uint32_t key = 1; };
extern std::map<std::wstring, TestFile> TestFiles;
extern std::vector<std::string> TestOperations;
extern std::string TestFailure;
struct TestHandle { std::wstring path; bool write; };
inline bool TestFail(const char* op) { return TestFailure == op; }
inline DWORD GetModuleFileName(void*, wchar_t* out, DWORD) { wcscpy(out, L"C:\\Reader\\Reader.exe"); return (DWORD)wcslen(out); }
inline DWORD GetFullPathName(const wchar_t* path, DWORD capacity, wchar_t* out, void*)
{
    std::wstring full(path);
    std::replace(full.begin(), full.end(), L'/', L'\\');
    if (full.size() < 2 || full[1] != L':') full = L"C:\\Reader\\" + full;
    std::vector<std::wstring> pieces;
    size_t start = 0;
    while (start < full.size())
    {
        size_t end = full.find(L'\\', start);
        if (end == std::wstring::npos) end = full.size();
        std::wstring part = full.substr(start, end - start);
        if (part == L".." && pieces.size() > 1) pieces.pop_back();
        else if (!part.empty() && part != L".") pieces.push_back(part);
        start = end + 1;
    }
    full.clear();
    for (size_t i = 0; i < pieces.size(); ++i) { if (i) full += L'\\'; full += pieces[i]; }
    if (full.size() < capacity) wcscpy(out, full.c_str());
    return (DWORD)full.size();
}
inline BOOL PathFileExists(const wchar_t* path) { return TestFiles.count(path) != 0; }
inline const wchar_t* PathFindFileName(const wchar_t* path)
{ const wchar_t* p = wcsrchr(path, L'\\'); return p ? p + 1 : path; }
inline void PathRemoveExtension(wchar_t* path) { wchar_t* p = wcsrchr(path, L'.'); if (p) *p = 0; }
inline HANDLE CreateFile(const wchar_t* path, DWORD access, DWORD, void*, DWORD mode, DWORD, void*)
{
    bool write = access == GENERIC_WRITE;
    if (TestFail(write ? "create" : "read_open")) return INVALID_HANDLE_VALUE;
    if (mode == OPEN_EXISTING && !TestFiles.count(path)) return INVALID_HANDLE_VALUE;
    if (mode == CREATE_ALWAYS) TestFiles[path].bytes.clear();
    return new TestHandle{path, write};
}
inline BOOL CloseHandle(HANDLE h) { delete (TestHandle*)h; return TRUE; }
inline BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER* size)
{ size->QuadPart = TestFiles[((TestHandle*)h)->path].bytes.size(); return TRUE; }
inline BOOL ReadFile(HANDLE h, void* out, DWORD size, DWORD* read, void*)
{
    auto& bytes = TestFiles[((TestHandle*)h)->path].bytes;
    *read = TestFail("read_short") ? size / 2 : size;
    memcpy(out, bytes.data(), *read); return TRUE;
}
inline BOOL WriteFile(HANDLE h, void* in, DWORD size, DWORD* written, void*)
{
    TestOperations.push_back("write");
    *written = TestFail("write_short") ? size / 2 : size;
    TestFiles[((TestHandle*)h)->path].bytes.assign((char*)in, (char*)in + *written);
    return !TestFail("write");
}
inline BOOL FlushFileBuffers(HANDLE) { TestOperations.push_back("flush"); return !TestFail("flush"); }
inline BOOL CopyFile(const wchar_t* source, const wchar_t* target, BOOL exclusive)
{
    TestOperations.push_back("backup");
    if (TestFail("backup") || !TestFiles.count(source) || (exclusive && TestFiles.count(target))) return FALSE;
    TestFiles[target] = TestFiles[source]; return TRUE;
}
inline BOOL MoveFileEx(const wchar_t* source, const wchar_t* target, DWORD)
{
    TestOperations.push_back("replace");
    if (TestFail("replace") || (TestFail("replace_primary") && std::wstring(target) == L"C:\\Reader\\.cache.dat") || !TestFiles.count(source)) return FALSE;
    TestFiles[target] = TestFiles[source]; TestFiles.erase(source); return TRUE;
}
inline BOOL DeleteFile(const wchar_t* path) { TestFiles.erase(path); return TRUE; }
inline BOOL SetFileAttributes(const wchar_t*, DWORD) { return TRUE; }
inline BOOL GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION* info)
{ info->dwVolumeSerialNumber = 3; info->nFileIndexHigh = 0; info->nFileIndexLow = TestFiles[((TestHandle*)h)->path].key; return TRUE; }
inline int MulDiv(int n, int mul, int div) { return n * mul / div; }
inline HDC GetDC(void*) { return NULL; }
inline int GetDeviceCaps(HDC, int) { return 96; }
inline HFONT CreateFont(int, int, int, int, int, BOOL, BOOL, BOOL, int, int, int, int, int, const wchar_t*) { return (HFONT)1; }
inline int GetObject(HFONT, int, LOGFONT* font) { memset(font, 0, sizeof(*font)); font->lfHeight = -19; wcscpy(font->lfFaceName, L"Test font"); return 1; }
