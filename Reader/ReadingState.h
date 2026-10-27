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
    ReadingAnchor(int b = -1, int c = -1, int p = 0, bool r = false)
        : book(b), chapter(c), offset(p), relative(r) {}
    bool operator==(const ReadingAnchor& a) const
    {
        return book == a.book && offset == a.offset && relative == a.relative
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

struct ReadingGeometry
{
    int toolbarHeight, columns, sidebarWidth;
    bool drawer;
    int readingLeft, readingTop, readingRight, readingBottom;
};

inline ReadingGeometry ComputeReadingGeometry(int width, int height, int dpi,
    bool chrome, bool toc)
{
    width = (std::max)(0, width); height = (std::max)(0, height);
    dpi = (std::max)(96, dpi);
    const int buttonWidth = 56 * dpi / 96;
    ReadingGeometry g = {};
    g.columns = (std::max)(1, (std::min)(8, width / buttonWidth));
    g.toolbarHeight = chrome ? (std::min)(height, ((8 + g.columns - 1) / g.columns) * 28 * dpi / 96) : 0;
    g.drawer = width < 700 * dpi / 96;
    g.sidebarWidth = toc ? (std::min)(width, (g.drawer ? 270 : 240) * dpi / 96) : 0;
    g.readingLeft = toc && !g.drawer ? g.sidebarWidth : 0;
    g.readingTop = g.toolbarHeight;
    g.readingRight = width;
    g.readingBottom = height;
    return g;
}
