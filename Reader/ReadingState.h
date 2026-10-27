#pragma once

#include <algorithm>
#include <vector>

inline bool ReaderOpenResultIsCurrent(unsigned current, unsigned received)
{
    return current != 0 && current == received;
}

class ReaderProgressObservation
{
    unsigned token;
    int position;
    bool observed;
public:
    ReaderProgressObservation() : token(0), position(0), observed(false) {}
    bool MarkRead(unsigned bookToken, int index, bool local, bool lastPage)
    {
        bool changed = !observed || token != bookToken || position != index;
        token = bookToken; position = index; observed = true;
        return changed && index > 0 && local && lastPage;
    }
};

// Reading locations are independent of library row order and permanent marks.
struct ReadingAnchor
{
    int book;
    int chapter;
    int offset;
    bool relative;
    int viewport, horizontal;
    ReadingAnchor(int b = -1, int c = -1, int p = 0, bool r = false, int v = -1, int h = -1)
        : book(b), chapter(c), offset(p), relative(r), viewport(v), horizontal(h) {}
    bool operator==(const ReadingAnchor& a) const
    {
        return book == a.book && offset == a.offset && relative == a.relative && viewport == a.viewport && horizontal == a.horizontal
            && (!relative || chapter == a.chapter);
    }
};

class ReadingHistory
{
    std::vector<ReadingAnchor> previous, next;
    static void Push(std::vector<ReadingAnchor>& list, const ReadingAnchor& a)
    {
        if (list.empty() || !(list.back() == a)) list.push_back(a);
        if (list.size() > 100) list.erase(list.begin());
    }
public:
    void Clear() { previous.clear(); next.clear(); }
    bool CanBack() const { return !previous.empty(); }
    bool CanForward() const { return !next.empty(); }
    void Record(const ReadingAnchor& from, const ReadingAnchor& to)
    {
        if (from.book < 0 || from == to) return;
        Push(previous, from);
        next.clear();
    }
    bool Back(const ReadingAnchor& current, ReadingAnchor& target)
    {
        if (previous.empty()) return false;
        target = previous.back(); previous.pop_back(); Push(next, current);
        return true;
    }
    bool Forward(const ReadingAnchor& current, ReadingAnchor& target)
    {
        if (next.empty()) return false;
        target = next.back(); next.pop_back(); Push(previous, current);
        return true;
    }
};

inline int ReaderScale(int logical,int dpi) { return (int)((long long)logical*dpi/96); }
inline int ReaderNavigationWidth(int width,int dpi,bool visible,bool panel,bool focus=false)
{ return visible?(std::min)((std::max)(0,width),ReaderScale(focus || panel || width<ReaderScale(700,dpi)?48:176,dpi)):0; }
inline int ReaderSidebarWidth(int clientWidth,int dpi,int logical)
{
    int maximum=(std::max)(0,(std::min)(ReaderScale(640,dpi),clientWidth-ReaderScale(326,dpi)));
    int minimum=(std::min)(ReaderScale(160,dpi),maximum);
    return (std::max)(minimum,(std::min)(maximum,ReaderScale(logical,dpi)));
}
struct ReadingGeometry
{
    int navigationWidth,sidebarWidth,splitterWidth,sidebarHeadingHeight;
    bool drawer;
    int readingLeft,readingTop,readingRight,readingBottom;
};
inline ReadingGeometry ComputeReadingGeometry(int width,int height,int dpi,bool chrome,bool panel,int sidebarLogical=240,bool focus=false)
{
    width=(std::max)(0,width);height=(std::max)(0,height);dpi=(std::max)(96,dpi);
    ReadingGeometry g={};g.navigationWidth=ReaderNavigationWidth(width,dpi,chrome,panel,focus);
    int available=width-g.navigationWidth;g.drawer=width<ReaderScale(700,dpi);
    g.sidebarHeadingHeight=panel?(std::min)(height,ReaderScale(36,dpi)):0;
    if(panel)
    {
        if(g.drawer) g.sidebarWidth=(std::min)((std::max)(0,available-ReaderScale(40,dpi)),ReaderScale((std::max)(160,(std::min)(360,sidebarLogical)),dpi));
        else {g.sidebarWidth=ReaderSidebarWidth(available,dpi,sidebarLogical);g.splitterWidth=ReaderScale(6,dpi);}
    }
    g.readingLeft=g.navigationWidth+(panel&&!g.drawer?g.sidebarWidth+g.splitterWidth:0);
    g.readingTop=0;g.readingRight=width;g.readingBottom=height;return g;
}
class ReaderSplitterDrag
{
    bool active; int original, originX, originWidth, dpi;
public:
    ReaderSplitterDrag() : active(false), original(240), originX(0), originWidth(0), dpi(96) {}
    bool Active() const { return active; }
    void Begin(int logical, int visible, int x, int scale)
    { original = logical; originWidth = visible; originX = x; dpi = (std::max)(96, scale); active = true; }
    int Move(int x, int clientWidth) const
    {
        if (!active) return original;
        int pixels = ReaderSidebarWidth(clientWidth, dpi, (int)((long long)(originWidth + x - originX) * 96 / dpi));
        return (int)((long long)pixels * 96 / dpi);
    }
    int Cancel() { active = false; return original; }
    bool Commit(int logical) { bool changed = active && logical != original; active = false; return changed; }
};
