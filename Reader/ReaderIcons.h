#pragma once
#include <vector>

// A small original set of 24-unit line icons. No font glyphs, brand assets or scaled bitmaps.
enum class ReaderIcon { Back = 1, Forward, Menu, Library, Reading, Read, Favorite, Missing,
    Search, AddFile, Folder, Open, Relink, ReadState, Remove, Refresh, Stop, Display, Bookmark, Toc, Focus };
enum class ReaderIconPrimitive { Line, Rectangle, Ellipse };
struct ReaderIconStroke { ReaderIconPrimitive type; float x1,y1,x2,y2; };
inline std::vector<ReaderIconStroke> ReaderIconStrokes(ReaderIcon icon)
{
    std::vector<ReaderIconStroke> p;
    auto line = [&](float a,float b,float c,float d) { p.push_back({ReaderIconPrimitive::Line,a,b,c,d}); };
    auto rect = [&](float a,float b,float c,float d) { p.push_back({ReaderIconPrimitive::Rectangle,a,b,c,d}); };
    auto ellipse = [&](float a,float b,float c,float d) { p.push_back({ReaderIconPrimitive::Ellipse,a,b,c,d}); };
    switch (icon)
    {
    case ReaderIcon::Back: line(19,12,5,12); line(5,12,11,6); line(5,12,11,18); break;
    case ReaderIcon::Forward: line(5,12,19,12); line(19,12,13,6); line(19,12,13,18); break;
    case ReaderIcon::Menu: for(int y : {6,12,18}) line(4,(float)y,20,(float)y); break;
    case ReaderIcon::Library: rect(4,4,8,20); rect(10,4,14,20); line(16,5,20,4); line(20,4,22,19); line(22,19,18,20); line(18,20,16,5); break;
    case ReaderIcon::Reading: ellipse(3,3,21,21); line(12,7,12,12); line(12,12,17,15); break;
    case ReaderIcon::Read: ellipse(3,3,21,21); line(7,12,10,15); line(10,15,17,8); break;
    case ReaderIcon::Favorite:
        { const float xy[][2]={{12,3},{15,9},{21,10},{16.5f,14.5f},{18,21},{12,17.5f},{6,21},{7.5f,14.5f},{3,10},{9,9},{12,3}};
          for(int i=0;i<10;++i) line(xy[i][0],xy[i][1],xy[i+1][0],xy[i+1][1]); } break;
    case ReaderIcon::Missing: line(12,3,22,21); line(22,21,2,21); line(2,21,12,3); line(12,9,12,14); ellipse(11.5f,17,12.5f,18); break;
    case ReaderIcon::Search: ellipse(3,3,16,16); line(15,15,21,21); break;
    case ReaderIcon::AddFile: rect(4,3,17,21); line(10.5f,8,10.5f,16); line(7,12,14,12); break;
    case ReaderIcon::Folder: line(3,7,3,20); line(3,20,21,20); line(21,20,21,7); line(21,7,12,7); line(12,7,9,4); line(9,4,3,4); line(3,4,3,7); break;
    case ReaderIcon::Open: line(12,6,12,21); line(12,6,4,4); line(4,4,3,19); line(3,19,12,21); line(12,6,20,4); line(20,4,21,19); line(21,19,12,21); break;
    case ReaderIcon::Relink: ellipse(3,7,12,17); ellipse(12,7,21,17); line(8,12,16,12); break;
    case ReaderIcon::ReadState: rect(4,3,20,21); line(8,8,16,8); line(8,12,16,12); line(8,16,13,16); break;
    case ReaderIcon::Remove: line(4,6,20,6); rect(7,6,17,21); line(9,3,15,3); line(10,10,10,17); line(14,10,14,17); break;
    case ReaderIcon::Refresh: line(20,9,20,3); line(20,9,14,9); line(20,9,16,5); line(16,5,8,4); line(8,4,4,9); line(4,9,4,16); line(4,16,8,20); line(8,20,16,20); line(16,20,20,15); break;
    case ReaderIcon::Stop: rect(6,6,18,18); break;
    case ReaderIcon::Display: ellipse(4,4,20,20); line(12,1,12,3); line(12,21,12,23); line(1,12,3,12); line(21,12,23,12); break;
    case ReaderIcon::Bookmark: line(6,3,18,3); line(18,3,18,21); line(18,21,12,17); line(12,17,6,21); line(6,21,6,3); break;
    case ReaderIcon::Focus: line(3,8,3,3);line(3,3,8,3);line(16,3,21,3);line(21,3,21,8);line(21,16,21,21);line(21,21,16,21);line(8,21,3,21);line(3,21,3,16);break;
    case ReaderIcon::Toc: for(int y : {6,12,18}) { line(4,(float)y,5,(float)y); line(9,(float)y,21,(float)y); } break;
    }
    return p;
}
