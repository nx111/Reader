#pragma once

#include "types.h"
#include <cwctype>
#include <string>

enum LibraryFilter { library_all, library_reading, library_read, library_favorite, library_missing };

inline std::wstring LibraryFold(const wchar_t* text)
{
    std::wstring s = text ? text : L"";
    for (size_t i = 0; i < s.size(); ++i) s[i] = (wchar_t)towlower(s[i]);
    return s;
}

inline bool LibraryMatches(const item_t& item, bool missing, int filter, const wchar_t* query)
{
    if ((filter == library_reading && item.read_state != 1)
        || (filter == library_read && item.read_state != 2)
        || (filter == library_favorite && !item.favorite)
        || (filter == library_missing && !missing)) return false;
    const std::wstring q = LibraryFold(query);
    return q.empty() || LibraryFold(item.title).find(q) != std::wstring::npos
        || LibraryFold(item.author).find(q) != std::wstring::npos
        || LibraryFold(item.file_name).find(q) != std::wstring::npos;
}

inline int LibraryCompareText(const wchar_t* a, const wchar_t* b)
{
    while (*a && *b && towlower(*a) == towlower(*b)) { ++a; ++b; }
    return towlower(*a) < towlower(*b) ? -1 : towlower(*a) > towlower(*b) ? 1 : 0;
}

inline int LibraryCompare(const item_t& a, const item_t& b, bool aMissing, bool bMissing, int column)
{
    switch (column)
    {
    case 0: return LibraryCompareText(a.title, b.title);
    case 1: return LibraryCompareText(a.author, b.author);
    case 2:
        {
            const wchar_t* x = wcsrchr(a.file_name, L'.');
            const wchar_t* y = wcsrchr(b.file_name, L'.');
            return LibraryCompareText(x ? x : L"", y ? y : L"");
        }
    case 3:
        {
            double x = a.text_length ? (double)a.index / a.text_length : -1;
            double y = b.text_length ? (double)b.index / b.text_length : -1;
            if (a.read_state == 2) x = 1;
            if (b.read_state == 2) y = 1;
            return x < y ? -1 : x > y ? 1 : 0;
        }
    case 4: return a.last_read < b.last_read ? -1 : a.last_read > b.last_read ? 1 : 0;
    case 5: return aMissing == bMissing ? a.read_state - b.read_state : (aMissing ? 1 : -1);
    default: return LibraryCompareText(a.file_name, b.file_name);
    }
}
