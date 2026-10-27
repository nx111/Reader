#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

struct ReaderTextChapter
{
    int start;
    std::wstring title;
    ReaderTextChapter(int p = 0, const std::wstring& t = L"") : start(p), title(t) {}
};

inline int ReaderChapterAt(const std::vector<ReaderTextChapter>& chapters, int position)
{
    int found = -1, start = -1;
    for (size_t i = 0; i < chapters.size(); ++i)
        if (chapters[i].start >= 0 && chapters[i].start <= position && chapters[i].start >= start)
        { start = chapters[i].start; found = (int)i; }
    return found;
}

inline bool ReaderHighSurrogate(wchar_t c) { return c >= 0xd800 && c <= 0xdbff; }
inline bool ReaderLowSurrogate(wchar_t c) { return c >= 0xdc00 && c <= 0xdfff; }

inline std::wstring ReaderClipText(const std::wstring& text, size_t limit)
{
    std::wstring result = text.substr(0, limit);
    if (!result.empty() && ReaderHighSurrogate(result.back())) result.pop_back();
    return result;
}

struct ReaderExcerpt
{
    std::wstring text;
    size_t matchStart, matchLength;
    ReaderExcerpt() : matchStart(0), matchLength(0) {}
};

inline ReaderExcerpt ReaderMakeExcerpt(const std::wstring& text, size_t position,
    size_t matchLength = 0, size_t context = 45)
{
    ReaderExcerpt result;
    if (position >= text.size()) return result;
    matchLength = (std::min)(matchLength, text.size() - position);
    size_t begin = position > context ? position - context : 0;
    size_t end = (std::min)(text.size(), position + matchLength + context);
    if (begin && ReaderLowSurrogate(text[begin]) && ReaderHighSurrogate(text[begin - 1])) --begin;
    if (end < text.size() && end && ReaderLowSurrogate(text[end]) && ReaderHighSurrogate(text[end - 1])) ++end;
    if (begin) result.text = L"…";
    size_t hitEnd = 0;
    for (size_t i = begin; i < end; ++i)
    {
        if (i == position) result.matchStart = result.text.size();
        wchar_t c = text[i];
        if (iswspace(c) || c < 0x20)
        {
            if (!result.text.empty() && result.text.back() != L' ') result.text += L' ';
        }
        else result.text += c;
        if (i + 1 == position + matchLength) hitEnd = result.text.size();
    }
    if (matchLength && hitEnd >= result.matchStart) result.matchLength = hitEnd - result.matchStart;
    while (!result.text.empty() && result.text.back() == L' ' && result.text.size() > hitEnd) result.text.pop_back();
    if (end < text.size()) result.text += L"…";
    return result;
}

struct ReaderSearchMatches
{
    size_t count;
    bool cancelled;
    std::vector<int> positions;
    ReaderSearchMatches() : count(0), cancelled(false) {}
};

// Literal, non-overlapping matches. A bounded visible list still reports the
// exact count; cancellation is checked regularly even when there are no hits.
template<class Cancel>
ReaderSearchMatches ReaderFindAll(const std::wstring& text, const std::wstring& query,
    bool matchCase, Cancel cancelled, size_t displayLimit = 5000)
{
    ReaderSearchMatches result;
    if (query.empty() || query.size() > text.size()) return result;
    size_t p = 0, checked = 0;
    while (p <= text.size() - query.size())
    {
        if ((checked++ & 255) == 0 && cancelled()) { result.cancelled = true; break; }
        size_t i = 0;
        for (; i < query.size(); ++i)
        {
            if ((i & 1023) == 1023 && cancelled()) { result.cancelled = true; return result; }
            wchar_t a = text[p + i], b = query[i];
            if (matchCase ? a != b : towlower(a) != towlower(b)) break;
        }
        if (i == query.size())
        {
            ++result.count;
            if (result.positions.size() < displayLimit) result.positions.push_back((int)p);
            p += query.size();
        }
        else ++p;
    }
    if (cancelled()) result.cancelled = true;
    return result;
}

// The token is owned by the UI document, never by a reusable Book* address.
struct ReaderSearchToken
{
    unsigned long long revision;
    int book, chapter;
    ReaderSearchToken() : revision(0), book(-1), chapter(-1) {}
    bool operator==(const ReaderSearchToken& a) const
    { return revision == a.revision && book == a.book && chapter == a.chapter; }
};

class ReaderSearchGate
{
    unsigned long generation;
    bool pending;
    ReaderSearchToken source;
public:
    ReaderSearchGate() : generation(0), pending(false) {}
    unsigned long Begin(const ReaderSearchToken& token)
    { source = token; pending = true; return ++generation; }
    void Cancel() { ++generation; pending = false; }
    bool Complete(unsigned long request, const ReaderSearchToken& token)
    {
        if (!pending || request != generation || !(source == token)) return false;
        pending = false;
        return true;
    }
};

enum ReaderSearchCommand { search_key_none, search_key_run, search_key_activate,
    search_key_cancel, search_key_close, search_key_previous, search_key_next,
    search_key_focus, search_key_click };

inline ReaderSearchCommand ReaderSearchKey(unsigned key, bool control, bool shift,
    bool inResults, bool onButton, bool pending)
{
    if (key == 27) return pending ? search_key_cancel : search_key_close;
    if (key == 13) return inResults ? search_key_activate : onButton ? search_key_click : search_key_run;
    if (key == 0x72) return shift ? search_key_previous : search_key_next; // F3
    if (control && key == 'F') return search_key_focus;
    return search_key_none;
}
