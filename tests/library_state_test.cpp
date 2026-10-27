#include "Cache.h"
#include "Jsondata.h"
#include "Keyset.h"
#include "LibraryModel.h"
#include "ReadingState.h"
#include <iostream>

std::map<std::wstring, TestFile> TestFiles;
std::vector<std::string> TestOperations;
std::string TestFailure;
static keyset_t* boundKeys = NULL;
void KS_UpdateKeyset(keyset_t* value) { boundKeys = value; }
void KS_GetDefaultKeyset(keyset_t* value) { memset(value, 0, MAX_KEYSET_COUNT * sizeof(keyset_t)); }
void GetCacheVersion(wchar_t* out) { wcscpy(out, L"2.0.0.4"); }
int GetCxScreenForDpi() { return 1920; }
int GetCyScreenForDpi() { return 1080; }

char* Utf16ToUtf8(const wchar_t* value)
{
    static std::string result;
    result = std::wstring_convert<std::codecvt_utf8<wchar_t> >().to_bytes(value);
    return const_cast<char*>(result.c_str());
}
wchar_t* Utf8ToUtf16(const char* value)
{
    static std::wstring result;
    try { result = std::wstring_convert<std::codecvt_utf8<wchar_t> >().from_bytes(value); }
    catch (...) { return NULL; }
    return const_cast<wchar_t*>(result.c_str());
}

static const wchar_t* primary = L"C:\\Reader\\.cache.dat";
static std::string Encoded(std::string value)
{ for (size_t i = 0; i < value.size(); ++i) value[i] ^= 0x3a; return value; }
static void Put(const wchar_t* path, const std::string& bytes)
{ TestFiles[path].bytes.assign(bytes.begin(), bytes.end()); }
static std::string Bytes(const wchar_t* path)
{ auto& b = TestFiles[path].bytes; return std::string(b.begin(), b.end()); }
static std::string Legacy()
{
    return R"({"header":{"version":"2.0.0.4","item_count":3,"item_id":2,"font":{"lfHeight":-21}},"items":[{"id":0,"index":149,"file_name":"C:\\books\\失联.epub","mark_size":3,"mark":[0,45,98],"is_new":0},{"id":1,"index":71,"file_name":"C:\\books\\second.txt","mark_size":1,"mark":[12],"is_new":0},{"id":2,"index":0,"file_name":"C:\\books\\third.azw3","mark_size":0,"mark":[],"is_new":0}]})";
}
static void Reset()
{ TestFailure.clear(); TestFiles.clear(); TestOperations.clear(); }

static void Migration()
{
    Reset(); const std::string original = Encoded(Legacy()); Put(primary, original);
    Cache cache(L".cache.dat"); assert(cache.init());
    assert(cache.get_header()->cache_schema == 2 && cache.get_header()->next_item_id == 4);
    assert(cache.get_header()->item_id == 1);
    assert(cache.get_header()->font.lfHeight == -21);
    for (int i = 0; i < 3; ++i)
    {
        const item_t* item = cache.get_item(i);
        assert(item->id == i + 1 && item->last_read == 0);
    }
    const item_t* first = cache.get_by_id(1);
    assert(first->index == 149 && first->mark_size == 3 && first->mark[2] == 98);
    assert(wcscmp(first->title, L"失联") == 0);
    assert(first->read_state == 1 && first->text_length == 0);
    assert(cache.save());
    assert(Bytes(L"C:\\Reader\\.cache.dat.v1.bak") == original);
    assert(Bytes(L"C:\\Reader\\.cache.dat.bak") == original);
    assert(Encoded(Bytes(primary)).find("\"schema_version\":2") != std::string::npos);
    assert(TestOperations[0] == "write" && TestOperations[1] == "flush");
    assert(cache.exit());
    Cache loaded(L".cache.dat"); assert(loaded.init());
    assert(loaded.get_item(0)->id == 1 && loaded.get_item(1)->index == 71);
    assert(loaded.get_item(0)->mark[0] == 0 && loaded.get_item(0)->last_read == 0);
    assert(loaded.get_header()->font.lfHeight == -21);
    loaded.get_by_id(1)->favorite = 1;
    assert(loaded.save());
    assert(Bytes(L"C:\\Reader\\.cache.dat.v1.bak") == original);
}

static void Invalid()
{
    const std::string examples[] = {
        "{", "{}", R"({"schema_version":3,"header":{"item_count":0}})",
        R"({"schema_version":2.5,"header":{"item_count":0}})",
        R"({"header":{"item_count":1},"items":[]})",
        R"({"header":{"item_count":-1}})",
        R"({"header":{"item_count":0,"book_source_count":1000}})",
        R"({"header":{"item_count":1},"items":[{"id":0,"index":4,"file_name":"C:\\a.txt","mark_size":2,"mark":[1]}]})",
        R"({"header":{"item_count":1},"items":[{"id":0,"index":-1,"file_name":"C:\\a.txt","mark_size":0,"mark":[]}]})",
        R"({"schema_version":2,"header":{"item_count":2,"item_id":1,"next_item_id":3},"items":[{"id":1,"index":0,"file_name":"C:\\a.txt","mark_size":0,"mark":[]},{"id":1,"index":0,"file_name":"C:\\b.txt","mark_size":0,"mark":[]}]})",
        R"({"header":{"item_count":0}}garbage)",
        std::string("{\"header\":{\"item_count\":1},\"items\":[{\"index\":0,\"file_name\":\"") + std::string(300, 'a') + "\",\"mark_size\":0,\"mark\":[]}]}"
    };
    for (const std::string& value : examples)
    {
        Reset(); const std::string original = Encoded(value); Put(primary, original);
        Cache cache(L".cache.dat"); assert(!cache.init());
        assert(Bytes(primary) == original && TestOperations.empty());
    }
    Reset(); Put(primary, Encoded(Legacy())); TestFailure = "read_short";
    Cache cache(L".cache.dat"); assert(!cache.init());
    assert(Bytes(primary) == Encoded(Legacy()));
}

static void Identity()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    const int a = cache.new_item(L"C:\\books\\a.txt")->id;
    const int b = cache.new_item(L"C:\\books\\b.epub")->id;
    const int c = cache.new_item(L"C:\\books\\c.azw3")->id;
    cache.get_by_id(a)->index = 42; cache.add_mark(cache.get_by_id(a), 28);
    assert(cache.open_item(c)->id == c && cache.get_item(0)->id == c);
    assert(cache.get_by_id(a)->index == 42 && cache.get_by_id(a)->mark[0] == 28);
    assert(cache.delete_item(b) && !cache.get_by_id(b));
    assert(cache.get_by_id(a)->id == a && cache.get_by_id(c)->id == c);
    assert(!cache.get_item(-1) && !cache.get_by_id(-1) && !cache.open_item(9999));
    assert(cache.delete_all_item());
    const int d = cache.new_item(L"C:\\books\\d.txt")->id;
    assert(d > c && !cache.get_by_id(a));
    assert(cache.save());
    Cache loaded(L".cache.dat"); assert(loaded.init());
    assert(loaded.get_by_id(d) && loaded.get_header()->next_item_id > d);
}

static void Dedup()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    int a = cache.new_item(L"C:\\books\\a.txt", L"vol:111")->id;
    assert(cache.new_item(L"c:/BOOKS/sub/../A.TXT")->id == a);
    assert(cache.new_item(L"C:\\elsewhere\\alias.txt", L"vol:111")->id == a);
    assert(cache.get_header()->item_count == 1);
    int b = cache.new_item(L"C:\\books\\b.txt")->id;
    assert(cache.new_item(L"C:\\books\\b.txt", L"vol:222")->id == b);
    assert(cache.find_item(L"C:\\moved\\b.txt", L"vol:222")->id == b);
    assert(!cache.new_item(std::wstring(300, L'x').c_str()));
    assert(cache.get_header()->item_count == 2);
}

static void Relink()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    int missing = cache.new_item(L"C:\\gone\\a.epub")->id;
    cache.get_by_id(missing)->index = 500;
    cache.add_mark(cache.get_by_id(missing), 10);
    int duplicate = cache.new_item(L"C:\\new\\a.epub", L"vol:444")->id;
    cache.get_by_id(duplicate)->index = 700; cache.get_by_id(duplicate)->favorite = 1;
    cache.get_by_id(duplicate)->last_read = 123456;
    cache.add_mark(cache.get_by_id(duplicate), 10); cache.add_mark(cache.get_by_id(duplicate), 40);
    cache.open_item(duplicate);
    assert(cache.relink_item(missing, L"C:\\new\\a.epub", L"vol:444"));
    const item_t* merged = cache.get_by_id(missing);
    assert(merged && !cache.get_by_id(duplicate) && cache.get_header()->item_count == 1);
    assert(cache.get_header()->item_id == missing);
    assert(merged->index == 500 && merged->mark_size == 3);
    assert(merged->mark[0] == 10 && merged->mark[1] == 40 && merged->mark[2] == 700);
    assert(merged->favorite && merged->last_read == 123456);
    assert(cache.find_item(L"c:/NEW/a.epub")->id == missing);
    assert(cache.relink_item(missing, L"C:\\renamed\\a.epub", L"vol:444"));
    assert(cache.get_by_id(missing)->index == 500);
    int other = cache.new_item(L"C:\\books\\full.txt")->id;
    item_t* full = cache.get_by_id(missing);
    full->mark_size = MAX_MARK_COUNT;
    for (int i = 0; i < MAX_MARK_COUNT; ++i) full->mark[i] = i + 1;
    cache.get_by_id(other)->index = 9999;
    const std::wstring oldPath = full->file_name;
    assert(!cache.relink_item(missing, L"C:\\books\\full.txt"));
    assert(cache.get_by_id(other) && cache.get_by_id(missing)->file_name == oldPath);
    assert(cache.get_by_id(missing)->mark_size == MAX_MARK_COUNT);
}

static void Persistence()
{
    const char* failures[] = { "create", "write", "write_short", "flush", "backup", "replace", "replace_primary" };
    for (const char* failure : failures)
    {
        Reset(); std::string original = Encoded(Legacy()); Put(primary, original);
        Cache cache(L".cache.dat"); assert(cache.init()); cache.get_by_id(1)->index = 321;
        TestFailure = failure;
        assert(!cache.save()); assert(Bytes(primary) == original);
        TestFailure.clear(); assert(cache.save());
        assert(Bytes(L"C:\\Reader\\.cache.dat.v1.bak") == original);
        Cache loaded(L".cache.dat"); assert(loaded.init()); assert(loaded.get_by_id(1)->index == 321);
    }
    Reset(); Put(primary, Encoded(Legacy()));
    Cache cache(L".cache.dat"); assert(cache.init()); cache.get_by_id(1)->index = 900;
    Put(primary, "external writer"); assert(!cache.save()); assert(Bytes(primary) == "external writer");
    Reset(); Cache fresh(L".cache.dat"); assert(fresh.init()); fresh.new_item(L"C:\\book.txt");
    TestFailure = "flush"; assert(!fresh.save()); assert(!TestFiles.count(primary));
    TestFailure.clear(); assert(fresh.save()); assert(TestFiles.count(primary));
}

static void Filters()
{
    item_t item = {}; wcscpy(item.title, L"测试 Reader"); wcscpy(item.author, L"作者 Alice"); wcscpy(item.file_name, L"C:\\books\\测试.AZW3");
    assert(LibraryMatches(item, false, library_all, L"READER"));
    assert(LibraryMatches(item, false, library_all, L"测试"));
    assert(LibraryMatches(item, false, library_all, L"alice"));
    assert(LibraryMatches(item, false, library_all, L"azw3"));
    assert(!LibraryMatches(item, false, library_all, L"unknown"));
    assert(!LibraryMatches(item, false, library_reading, L""));
    item.read_state = 1; assert(LibraryMatches(item, false, library_reading, L""));
    assert(!LibraryMatches(item, false, library_read, L""));
    item.read_state = 2; assert(LibraryMatches(item, false, library_read, L""));
    item.favorite = 1; assert(LibraryMatches(item, true, library_favorite, L""));
    assert(LibraryMatches(item, true, library_missing, L""));
    assert(!LibraryMatches(item, false, library_missing, L""));
    item_t other = item;
    wcscpy(other.title, L"Z book"); wcscpy(other.author, L"Z author");
    wcscpy(item.title, L"A book"); wcscpy(item.author, L"A author");
    wcscpy(item.file_name, L"C:\\a.epub"); wcscpy(other.file_name, L"C:\\z.txt");
    item.last_read = 0; other.last_read = 120;
    item.read_state = 1; item.index = 10; item.text_length = 100;
    other.read_state = 2;
    for (int column = 0; column < 7; ++column)
    {
        assert(LibraryCompare(item, other, false, true, column) < 0);
        assert(LibraryCompare(other, item, true, false, column) > 0);
        assert(LibraryCompare(item, item, false, false, column) == 0);
    }
}

static void Navigation()
{
    ReadingHistory h;
    ReadingAnchor a(7, 0, 20), b(7, 1, 500), c(7, 3, 1500), live(7, 3, 1530), target;
    h.Record(a, b); h.Record(b, c);
    assert(h.Back(live, target) && target == b);
    assert(h.Back(b, target) && target == a);
    assert(h.Forward(a, target) && target == b);
    h.Record(b, b); assert(h.CanForward());
    assert(h.Forward(b, target) && target == live);
    assert(h.Back(live, target) && target == b);
    ReadingAnchor next(7, 6, 3000); h.Record(b, next); assert(!h.CanForward());
    h.Clear(); assert(!h.CanBack() && !h.CanForward());
    ReadingAnchor web1(9, 2, 25, true), web2(9, 4, 25, true);
    assert(!(web1 == web2)); h.Record(web1, web2);
    assert(h.Back(web2, target) && target == web1 && target.chapter == 2 && target.offset == 25);
    h.Clear();
    for (int i = 0; i < 120; ++i) h.Record(ReadingAnchor(7, 0, i), ReadingAnchor(7, 0, i + 1));
    int count = 0;
    while (h.Back(ReadingAnchor(7, 0, 120 - count), target)) ++count;
    assert(count == 100 && target.offset == 20);
}

static void Geometry()
{
    for (int dpi : { 96, 120, 144, 192 })
    {
        auto narrow = ComputeReadingGeometry(300 * dpi / 96, 500 * dpi / 96, dpi, true, true);
        assert(narrow.drawer && narrow.readingLeft == 0 && narrow.toolbarHeight == 56 * dpi / 96);
        assert(narrow.sidebarWidth <= 300 * dpi / 96 && narrow.readingTop == narrow.toolbarHeight);
        auto closed = ComputeReadingGeometry(1000 * dpi / 96, 600 * dpi / 96, dpi, true, false);
        auto wide = ComputeReadingGeometry(1000 * dpi / 96, 600 * dpi / 96, dpi, true, true);
        assert(!wide.drawer && wide.readingLeft == 240 * dpi / 96);
        assert(wide.readingRight == closed.readingRight && wide.readingTop == closed.readingTop);
        auto focus = ComputeReadingGeometry(300 * dpi / 96, 500 * dpi / 96, dpi, false, false);
        assert(focus.toolbarHeight == 0 && focus.sidebarWidth == 0 && focus.readingLeft == 0);
        auto tiny = ComputeReadingGeometry(8, 8, dpi, true, true);
        assert(tiny.readingBottom >= tiny.readingTop && tiny.sidebarWidth <= 8);
    }
}

static void Large()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    int id = cache.new_item(L"C:\\active.txt")->id;
    cache.get_by_id(id)->index = 932; cache.add_mark(cache.get_by_id(id), 123);
    cache.open_item(id);
    for (int i = 0; i < 20000; ++i)
    {
        std::wstring path = L"C:\\books\\" + std::to_wstring(i) + L".txt";
        assert(cache.new_item(path.c_str()));
    }
    assert(cache.get_header()->item_count == 20001 && cache.get_header()->item_id == id);
    assert(cache.get_by_id(id)->index == 932 && cache.get_by_id(id)->mark[0] == 123);
    assert(boundKeys == cache.get_header()->keyset);
    assert(cache.open_item(cache.get_item(15000)->id));
    assert(cache.get_by_id(id)->index == 932);
}

namespace Bridge {
enum { book_text, book_online, book_web };
struct Chapter { int index; };
struct StubBook
{
    int* position = NULL;
    header_t* header = NULL;
    int type = book_text, chapter = 0, request = -1;
    bool loading = false, asynchronous = false;
    std::vector<Chapter> chapters = { {0}, {100}, {300} };
    void Rebind(int* p, header_t* h) { position = p; header = h; }
    void SetChapterRule(chapter_rule_t*) {}
    void SetFileName(const wchar_t*) {}
    bool IsLoading() { return loading; }
    int GetBookType() { return type; }
    int GetCurChapterIndex()
    {
        if (type != book_text) return chapter;
        int found = 0;
        for (int i = 0; i < (int)chapters.size(); ++i) if (chapters[i].index <= *position) found = i;
        return found;
    }
    std::vector<Chapter>* GetChapters() { return &chapters; }
    void SetReadingPosition(int value) { *position = (std::max)(0, (std::min)(999, value)); }
    void ReDraw(HWND) {}
    void JumpChapter(HWND, int value)
    {
        if (asynchronous) { loading = true; request = value; return; }
        chapter = value; *position = chapters[value].index;
    }
    void Complete(bool success)
    {
        loading = false;
        if (success) { chapter = request; *position = chapters[chapter].index; }
    }
};
static Cache _Cache(L".cache.dat");
static header_t* _header = NULL;
static item_t* _item = NULL;
static StubBook* _Book = NULL;
static int _activeBookId = -1;
static HWND _readerButtons[8] = {};
static ReadingHistory _readingHistory, _replayHistory;
static ReadingAnchor _jumpOrigin, _restoreAnchor;
static BOOL _jumpPending = FALSE, _restorePending = FALSE, _replaying = FALSE;
static BOOL EC_IsEditMode() { return FALSE; }
static BOOL EnableWindow(HWND, BOOL) { return TRUE; }
static void FinishReadingJump();
static void PrepareReadingJump(HWND);
#include "reader_bridges.inc"

static void Import()
{
    Reset(); assert(_Cache.init());
    _activeBookId = _Cache.new_item(L"C:\\reading.txt")->id;
    _Cache.get_by_id(_activeBookId)->index = 321;
    StubBook book; _Book = &book; RebindReadingCache();
    for (int batch = 0; batch < 100; ++batch)
    {
        std::vector<item_t> entries(24);
        for (int i = 0; i < 24; ++i)
        {
            std::wstring name = L"C:\\import\\" + std::to_wstring(batch * 24 + i) + L".epub";
            wcscpy(entries[i].file_name, name.c_str()); wcscpy(entries[i].title, L"Imported title");
        }
        ImportLibraryEntries(entries);
        assert(_item == _Cache.get_by_id(_activeBookId) && _item->index == 321);
        assert(book.position == &_item->index && book.header == _Cache.get_header());
    }
    assert(_Cache.get_header()->item_count == 2401);
    _Book = NULL;
}

static void NavigationBridge()
{
    Reset(); assert(_Cache.init());
    _activeBookId = _Cache.new_item(L"C:\\reading.txt")->id;
    StubBook book; _Book = &book; RebindReadingCache(); _item->index = 12;
    _Cache.add_mark(_item, 7);
    NavigatePosition(NULL, 140); FinishReadingJump();
    _item->index = 160; // Ordinary page turns do not create extra jump-history entries.
    NavigateHistory(NULL, FALSE); FinishReadingJump(); assert(_item->index == 12);
    NavigateHistory(NULL, TRUE); FinishReadingJump(); assert(_item->index == 160);
    assert(_item->mark_size == 1 && _item->mark[0] == 7);
    _readingHistory.Clear();
    book.type = book_web; book.asynchronous = true; book.chapter = 2;
    book.chapters = { {0}, {-1}, {0} }; _item->index = 40;
    ReadingAnchor origin = CurrentReadingAnchor();
    _readingHistory.Record(ReadingAnchor(_activeBookId, 0, 19, true), origin);
    NavigateHistory(NULL, FALSE); assert(book.loading && _restorePending);
    book.Complete(false); PrepareReadingJump(NULL); FinishReadingJump();
    assert(_readingHistory.CanBack() && !_readingHistory.CanForward() && _item->index == 40);
    NavigateHistory(NULL, FALSE); book.Complete(true);
    PrepareReadingJump(NULL); FinishReadingJump();
    assert(book.chapter == 0 && _item->index == 19 && _readingHistory.CanForward());
    NavigateHistory(NULL, TRUE); book.Complete(true);
    PrepareReadingJump(NULL); FinishReadingJump();
    assert(book.chapter == 2 && _item->index == 40);
    assert(_item->mark_size == 1 && _item->mark[0] == 7);
    _Book = NULL;
}
} // namespace Bridge

int main(int argc, char** argv)
{
    assert(argc == 2); std::string test = argv[1];
    if (test == "migration") Migration(); else if (test == "invalid") Invalid();
    else if (test == "identity") Identity(); else if (test == "dedup") Dedup();
    else if (test == "relink") Relink(); else if (test == "persistence") Persistence();
    else if (test == "filters") Filters(); else if (test == "navigation") Navigation();
    else if (test == "geometry") Geometry(); else if (test == "large") Large();
    else if (test == "import_bridge") Bridge::Import();
    else if (test == "navigation_bridge") Bridge::NavigationBridge();
    else return 2;
    std::cout << test << " passed\n";
}
