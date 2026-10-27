#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

struct ReaderPdfViewport { int width, height, pageWidth, pageHeight, left, top, maxX, maxY; double scale; };
inline ReaderPdfViewport ReaderPdfLayout(double paperWidth, double paperHeight, int width, int height,
    int zoom, int dpi, int x, int y)
{
    ReaderPdfViewport g = {}; g.width=(std::max)(1,width);g.height=(std::max)(1,height);
    if (!std::isfinite(paperWidth) || !std::isfinite(paperHeight) || paperWidth <= 0 || paperHeight <= 0) return g;
    // The reading child supplies the already-inset content viewport.
    double fit=g.width/paperWidth;
    g.scale=zoom==0?fit:zoom==-1?(std::min)(fit,g.height/paperHeight):zoom/100.0*(std::max)(96,dpi)/72;
    g.pageWidth=(int)(std::min)(1000000.0,(std::max)(1.0,paperWidth*g.scale));
    g.pageHeight=(int)(std::min)(1000000.0,(std::max)(1.0,paperHeight*g.scale));
    g.maxX=(std::max)(0,g.pageWidth-g.width);g.maxY=(std::max)(0,g.pageHeight-g.height);
    // Normalize to the document page, so resizing the viewport keeps the same
    // point on the paper at its top/left instead of shifting with scroll range.
    g.left=g.maxX?-(int)(std::min)((int64_t)g.maxX,(int64_t)g.pageWidth*(std::max)(0,(std::min)(1000000,x))/1000000):(g.width-g.pageWidth)/2;
    g.top=g.maxY?-(int)(std::min)((int64_t)g.maxY,(int64_t)g.pageHeight*(std::max)(0,(std::min)(1000000,y))/1000000):(g.height-g.pageHeight)/2;
    return g;
}
inline int ReaderPdfNormalize(int pixels, int maximum)
{ return maximum>0?(int)((int64_t)(std::max)(0,(std::min)(maximum,pixels))*1000000/maximum):0; }
inline int ReaderPdfZoom(int current, double scale, int dpi, int direction)
{
    int percent=current>0?current:(int)(scale*72/(std::max)(96,dpi)*100+0.5);
    return (std::max)(25,(std::min)(400,percent+direction*25));
}
