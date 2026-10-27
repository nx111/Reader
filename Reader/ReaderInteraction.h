#pragma once
#include <algorithm>
#include <cstdint>

class ReaderCtrlWheel
{
    int remainder=0;uint64_t book=0;
public:
    void Reset() {remainder=0;book=0;}
    int Feed(int delta,bool control,bool reading,bool capture,uint64_t token)
    {
        if(!control || !reading || capture || !token) {Reset();return 0;}
        if(book!=token) {remainder=0;book=token;}
        int total=remainder+delta,steps=total/120;remainder=total%120;return steps;
    }
};
inline int ReaderFontPoints(int current,int steps)
{return (int)(std::max)(int64_t(1),(std::min)(int64_t(72),(int64_t)current+steps));}

// The expanded sidebar icon inset, scaled once using MulDiv's positive rounding.
inline int ReaderSidebarPadding(int dpi) { return (14*(std::max)(96,dpi)+48)/96; }
inline int ReaderReadingPadding(int dpi) { return 2*ReaderSidebarPadding(dpi); }
template<class Rect> inline Rect ReaderReadingInsets(Rect configured,int dpi,bool custom)
{
    if(!custom) configured.left=configured.top=configured.right=configured.bottom=ReaderReadingPadding(dpi);
    return configured;
}
template<class Rect> inline bool ReaderApplyReadingInsetEdit(Rect& configured,int& custom,const Rect& shown,const Rect& entered)
{
    if(shown.left==entered.left && shown.top==entered.top && shown.right==entered.right && shown.bottom==entered.bottom) return false;
    configured=entered;custom=1;return true;
}
template<class Rect> inline Rect ReaderReadingContentRect(Rect bounds,int dpi,const Rect& configured,bool custom=false)
{
    // Old cached values stay intact but only a new, explicit edit can override
    // the uniform sidebar-derived standard. Clamp tiny viewports to a pixel.
    Rect inset=ReaderReadingInsets(configured,dpi,custom);
    int x=(std::max)(0,(int)(bounds.right-bounds.left)-1)/2;
    int y=(std::max)(0,(int)(bounds.bottom-bounds.top)-1)/2;
    if(!custom) x=y=(std::min)(x,y); // Keep all four automatic insets equal even in a tiny viewport.
    bounds.left+=(std::min)(x,(std::max)(0,(int)inset.left));
    bounds.right-=(std::min)(x,(std::max)(0,(int)inset.right));
    bounds.top+=(std::min)(y,(std::max)(0,(int)inset.top));
    bounds.bottom-=(std::min)(y,(std::max)(0,(int)inset.bottom));
    return bounds;
}

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
