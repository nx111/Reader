#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using BYTE = unsigned char;
using DWORD = uint32_t;
using COLORREF = uint32_t;
using UINT = unsigned;
using BOOL = int;
using HWND = intptr_t;
using LRESULT = intptr_t;
using VOID = void;
using TCHAR = wchar_t;
using HTREEITEM = intptr_t;
const BOOL TRUE = 1, FALSE = 0;
struct RECT { int left, top, right, bottom; };
struct GdiObject { enum Kind { bitmap, font, brush } kind; explicit GdiObject(Kind k) : kind(k) {} virtual ~GdiObject() {} };
struct FakeBitmap : GdiObject { int width, height; std::vector<BYTE> pixels; FakeBitmap(int w, int h) : GdiObject(bitmap), width(w), height(h), pixels((size_t)w*h*4) {} };
struct FakeDC;
using HGDIOBJ = GdiObject*;
using HBITMAP = FakeBitmap*;
using HBRUSH = GdiObject*;
using HFONT = GdiObject*;
using HDC = FakeDC*;
static FakeBitmap originalBitmap(0, 0);
static GdiObject originalFont(GdiObject::font);
struct FakeDC {
    HGDIOBJ bitmap = &originalBitmap, font = &originalFont;
    RECT clip={0,0,1000000,1000000}; std::vector<RECT> saved;
};
inline int SaveDC(HDC dc) { dc->saved.push_back(dc->clip);return (int)dc->saved.size(); }
inline int IntersectClipRect(HDC dc,int left,int top,int right,int bottom)
{
    dc->clip={(std::max)(dc->clip.left,left),(std::max)(dc->clip.top,top),
              (std::min)(dc->clip.right,right),(std::min)(dc->clip.bottom,bottom)};return 1;
}
inline BOOL RestoreDC(HDC dc,int saved)
{
    assert(saved>0&&saved<=(int)dc->saved.size());dc->clip=dc->saved[saved-1];dc->saved.resize(saved-1);return TRUE;
}
static std::vector<HDC> liveDCs;
static int createdDCs = 0, deletedDCs = 0, createdDIBs = 0, deletedDIBs = 0;
static bool failDIB = false;
#define HGDI_ERROR ((HGDIOBJ)(intptr_t)-1)
struct BITMAPINFOHEADER { DWORD biSize; int biWidth, biHeight; unsigned short biPlanes, biBitCount; DWORD biCompression; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; };
const int BI_RGB = 0, DIB_RGB_COLORS = 0;
inline HDC CreateCompatibleDC(HDC) { auto dc = new FakeDC; liveDCs.push_back(dc); ++createdDCs; return dc; }
inline HBITMAP CreateDIBSection(HDC, BITMAPINFO* info, int, void** pixels, void*, int)
{
    if (failDIB) return nullptr;
    auto bitmap = new FakeBitmap(info->bmiHeader.biWidth, info->bmiHeader.biHeight);
    *pixels = bitmap->pixels.data(); ++createdDIBs; return bitmap;
}
inline HGDIOBJ SelectObject(HDC dc, HGDIOBJ object)
{
    auto& current = object->kind == GdiObject::bitmap ? dc->bitmap : dc->font;
    HGDIOBJ previous = current; current = object; return previous;
}
inline void DeleteObject(HGDIOBJ object)
{
    for (auto dc : liveDCs) assert(dc->bitmap != object && dc->font != object);
    if (object->kind == GdiObject::bitmap) ++deletedDIBs;
    delete object;
}
inline void DeleteDC(HDC dc)
{
    assert(dc->bitmap == &originalBitmap && dc->font == &originalFont && dc->saved.empty());
    liveDCs.erase(std::find(liveDCs.begin(),liveDCs.end(),dc)); ++deletedDCs; delete dc;
}
inline HBRUSH CreateSolidBrush(COLORREF) { return new GdiObject(GdiObject::brush); }
inline void FillRect(HDC dc, const RECT* rect, HBRUSH)
{
    auto bitmap = static_cast<FakeBitmap*>(dc->bitmap);
    assert(rect->left == 0 && rect->top == 0 && rect->right == bitmap->width && rect->bottom == bitmap->height);
}
