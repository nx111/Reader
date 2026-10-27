#pragma once
#include <cstdint>

#define WINAPI
using BOOL=int;
using UINT=unsigned;
using DWORD=uint32_t;
using COLORREF=uint32_t;
using HRESULT=int32_t;
using LONG_PTR=intptr_t;
using HWND=void*;
using HMODULE=void*;
using LPCVOID=const void*;
using FARPROC=intptr_t(*)();
struct HIGHCONTRASTW { UINT cbSize; DWORD dwFlags; wchar_t* lpszDefaultScheme; };
const int MAX_PATH=260,GWL_STYLE=-16;
const LONG_PTR WS_CHILD=0x40000000;
const UINT SPI_GETHIGHCONTRAST=66;
const DWORD HCF_HIGHCONTRASTON=1;
#define SUCCEEDED(hr) ((hr)>=0)
#define FAILED(hr) ((hr)<0)
UINT GetSystemDirectoryW(wchar_t*,UINT);
HMODULE LoadLibraryW(const wchar_t*);
FARPROC GetProcAddress(HMODULE,const char*);
BOOL FreeLibrary(HMODULE);
LONG_PTR GetWindowLongPtrW(HWND,int);
BOOL SystemParametersInfoW(UINT,UINT,void*,UINT);
