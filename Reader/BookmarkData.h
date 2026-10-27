#pragma once

#include <map>
#include <string>
#include <vector>

// Sparse metadata preserves legacy mark[] positions when reading cache v1/v2.
// chapter >= 0 means that position is relative to that online chapter.
struct bookmark_detail_t
{
    int chapter;
    int position;
    std::wstring title, summary;
    bookmark_detail_t(int c = -1, int p = 0) : chapter(c), position(p) {}
};
typedef std::map<int, std::vector<bookmark_detail_t> > bookmark_store_t;
