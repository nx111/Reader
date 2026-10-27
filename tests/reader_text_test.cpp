#include "ReaderText.h"
#include <cassert>
#include <iostream>

static void Search()
{
    std::wstring text = L"第一章\nAlpha 中文 target\n第二章\nTARGET target";
    auto insensitive = ReaderFindAll(text, L"target", false, []() { return false; });
    assert(insensitive.count == 3 && insensitive.positions.size() == 3);
    auto exact = ReaderFindAll(text, L"target", true, []() { return false; });
    assert(exact.count == 2);
    auto chinese = ReaderFindAll(text, L"中文", true, []() { return false; });
    assert(chinese.count == 1 && text.substr(chinese.positions[0], 2) == L"中文");
    std::vector<ReaderTextChapter> chapters = { {0, L"第一章"}, {(int)text.find(L"第二章"), L"第二章"} };
    assert(ReaderChapterAt(chapters, insensitive.positions[0]) == 0);
    assert(ReaderChapterAt(chapters, insensitive.positions[2]) == 1);
    assert(ReaderFindAll(L"aaaa", L"aa", true, []() { return false; }).count == 2);
    assert(ReaderFindAll(L"a.b a*b", L"a.b", true, []() { return false; }).count == 1);
}

static void Bounds()
{
    auto many = ReaderFindAll(std::wstring(25000, L'x'), L"x", true, []() { return false; }, 7);
    assert(many.count == 25000 && many.positions.size() == 7);
    for (int i = 0; i < 7; ++i) assert(many.positions[i] == i);
    assert(ReaderFindAll(L"text", L"", true, []() { return false; }).count == 0);
    assert(ReaderFindAll(L"", L"x", true, []() { return false; }).count == 0);
    assert(ReaderFindAll(L"abc", L"abcd", true, []() { return false; }).count == 0);
    assert(ReaderFindAll(L"text", L"not found", true, []() { return false; }).positions.empty());
}

static void Cancellation()
{
    int calls = 0;
    auto noHits = ReaderFindAll(std::wstring(100000, L'a'), L"z", false, [&]() { return ++calls >= 2; });
    assert(noHits.cancelled && noHits.count == 0 && calls < 5);
    calls = 0;
    auto many = ReaderFindAll(std::wstring(100000, L'a'), L"a", true, [&]() { return ++calls >= 2; });
    assert(many.cancelled && many.count > 0 && many.count < 1000);
    calls = 0;
    auto longTerm = ReaderFindAll(std::wstring(100000, L'a'), std::wstring(10000, L'a'), true, [&]() { return ++calls >= 2; });
    assert(longTerm.cancelled && longTerm.count == 0);
}

static void Excerpt()
{
    std::wstring text = L"  前文\r\n  关键词\t后文  ";
    size_t position = text.find(L"关键词");
    ReaderExcerpt excerpt = ReaderMakeExcerpt(text, position, 3, 20);
    assert(excerpt.text.find(L'\n') == std::wstring::npos && excerpt.text.find(L'\t') == std::wstring::npos);
    assert(excerpt.text.substr(excerpt.matchStart, excerpt.matchLength) == L"关键词");
    assert(excerpt.text.find(L"  ") == std::wstring::npos);
    std::wstring utf16 = L"x\xd83d\xde00yz";
    ReaderExcerpt emoji = ReaderMakeExcerpt(utf16, 3, 1, 1);
    assert(emoji.text.find(L"\xd83d\xde00") != std::wstring::npos);
    assert(emoji.text.substr(emoji.matchStart, emoji.matchLength) == L"y");
    ReaderExcerpt edge = ReaderMakeExcerpt(utf16, 0, 1, 1);
    assert(edge.text.find(L"\xd83d\xde00") != std::wstring::npos);
    assert(ReaderMakeExcerpt(text, text.size(), 1).text.empty());
    auto last = ReaderMakeExcerpt(L"end", 2, 1000);
    assert(last.text.substr(last.matchStart, last.matchLength) == L"d");
    assert(ReaderClipText(L"x\xd83d\xde00y", 2) == L"x");
    assert(ReaderClipText(L"x\xd83d\xde00y", 3) == L"x\xd83d\xde00");
}

static void Gate()
{
    ReaderSearchToken document; document.book = 5; document.revision = 10;
    ReaderSearchGate gate;
    auto old = gate.Begin(document), latest = gate.Begin(document);
    assert(!gate.Complete(old, document) && gate.Complete(latest, document));
    assert(!gate.Complete(latest, document)); // Repeated delivery cannot publish twice.
    auto cancelled = gate.Begin(document); gate.Cancel();
    assert(!gate.Complete(cancelled, document));
    auto stale = gate.Begin(document); ReaderSearchToken changed = document; ++changed.revision;
    assert(!gate.Complete(stale, changed));
    auto current = gate.Begin(changed); assert(gate.Complete(current, changed));
    changed.chapter = 2; current = gate.Begin(changed);
    ReaderSearchToken another = changed; another.chapter = 3;
    assert(!gate.Complete(current, another));
}

static void Chapters()
{
    std::vector<ReaderTextChapter> chapters = { {-1, L"未加载"}, {10, L"第二章"}, {100, L"第三章"} };
    assert(ReaderChapterAt(chapters, 0) == -1);
    assert(ReaderChapterAt(chapters, 10) == 1);
    assert(ReaderChapterAt(chapters, 99) == 1);
    assert(ReaderChapterAt(chapters, 100) == 2);
    assert(ReaderChapterAt({}, 20) == -1);
}

static void Keys()
{
    assert(ReaderSearchKey(13, false, false, false, false, false) == search_key_run);
    assert(ReaderSearchKey(13, false, false, true, false, false) == search_key_activate);
    assert(ReaderSearchKey(13, false, false, false, true, true) == search_key_click); // Cancel button Enter must not restart a search.
    assert(ReaderSearchKey(27, false, false, false, false, true) == search_key_cancel);
    assert(ReaderSearchKey(27, false, false, false, false, false) == search_key_close);
    assert(ReaderSearchKey(0x72, false, false, true, false, false) == search_key_next);
    assert(ReaderSearchKey(0x72, false, true, true, false, false) == search_key_previous);
    assert(ReaderSearchKey('F', true, false, false, false, false) == search_key_focus);
    assert(ReaderSearchKey(9, false, false, false, false, false) == search_key_none); // Tab stays with native accessible controls.
}

int main(int argc, char** argv)
{
    assert(argc == 2); std::string test(argv[1]);
    if (test == "search") Search(); else if (test == "bounds") Bounds();
    else if (test == "cancel") Cancellation(); else if (test == "excerpt") Excerpt();
    else if (test == "gate") Gate(); else if (test == "chapters") Chapters();
    else if (test == "keys") Keys();
    else return 2;
    std::cout << test << " passed\n";
}
