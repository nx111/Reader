#pragma once
#include "framework.h"
#include <algorithm>
#include "ReaderIcons.h"
inline void DrawReaderIcon(HDC dc, ReaderIcon icon, const RECT& bounds, COLORREF color)
{
    float size = (float)(std::min)(bounds.right-bounds.left,bounds.bottom-bounds.top);
    if (size <= 0) return;
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.TranslateTransform((bounds.left+bounds.right-size)/2.f,(bounds.top+bounds.bottom-size)/2.f);
    g.ScaleTransform(size/24.f,size/24.f);
    Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),1.65f);
    pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
    for (const auto& p : ReaderIconStrokes(icon))
    {
        if (p.type == ReaderIconPrimitive::Line) g.DrawLine(&pen,p.x1,p.y1,p.x2,p.y2);
        else if (p.type == ReaderIconPrimitive::Rectangle) g.DrawRectangle(&pen,p.x1,p.y1,p.x2-p.x1,p.y2-p.y1);
        else g.DrawEllipse(&pen,p.x1,p.y1,p.x2-p.x1,p.y2-p.y1);
    }
}
