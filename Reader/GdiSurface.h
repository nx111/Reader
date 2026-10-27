#pragma once
#include "framework.h"

// A DIB is replaced only when its size changes, and is deselected before free.
class GdiSurface
{
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
    BYTE* pixels;
    int width, height;
public:
    GdiSurface() : dc(NULL), bitmap(NULL), previous(NULL), pixels(NULL), width(0), height(0) {}
    ~GdiSurface() { Reset(); }
    GdiSurface(const GdiSurface&) = delete;
    GdiSurface& operator=(const GdiSurface&) = delete;
    bool Ensure(HDC reference, int w, int h)
    {
        if (w <= 0 || h <= 0) return false;
        if (bitmap && width == w && height == h) return true;
        if (!dc) dc = CreateCompatibleDC(reference);
        if (!dc) return false;
        BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        BYTE* nextPixels = NULL;
        HBITMAP next = CreateDIBSection(reference, &info, DIB_RGB_COLORS, (void**)&nextPixels, NULL, 0);
        if (!next) return false;
        HGDIOBJ old = SelectObject(dc, next);
        if (!old || old == HGDI_ERROR) { DeleteObject(next); return false; }
        if (!previous) previous = old;
        if (bitmap) DeleteObject(bitmap);
        bitmap = next; pixels = nextPixels; width = w; height = h;
        return true;
    }
    void Reset()
    {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = NULL; bitmap = NULL; previous = NULL; pixels = NULL; width = height = 0;
    }
    HDC DC() const { return dc; }
    BYTE* Pixels() const { return pixels; }
};
