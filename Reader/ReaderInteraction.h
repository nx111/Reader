#pragma once
#include <algorithm>

inline int ReaderItemPadding(int dpi) { return (std::max)(1,8*(std::max)(96,dpi)/96); }
inline int ReaderItemInset(int width,int dpi)
{ return (std::min)(ReaderItemPadding(dpi),(std::max)(0,(width-1)/2)); }
inline bool ReaderRailClear(bool focusMode,bool active,bool pointer,bool focus,bool menu,bool capture)
{ return !focusMode || (active && (pointer || focus || menu || capture)); }
enum ReaderEscapeAction { reader_escape_none, reader_escape_drag, reader_escape_edit,
    reader_escape_panel, reader_escape_navigation, reader_escape_focus };
inline ReaderEscapeAction ReaderEscapePriority(bool modal,bool editing,bool drag,bool panel,bool navigation,bool focus)
{
    if(modal) return reader_escape_none;
    if(drag) return reader_escape_drag;
    if(editing) return reader_escape_edit;
    if(panel) return reader_escape_panel;
    if(navigation) return reader_escape_navigation;
    return focus?reader_escape_focus:reader_escape_none;
}
struct ReaderFocusPanels
{
    bool saved=false,toc=false,marks=false;
    void Capture(bool currentToc,bool currentMarks)
    { if(!saved) {saved=true;toc=currentToc;marks=currentMarks;} }
    void Clear() {saved=toc=marks=false;}
};
