#include "Cache.h"
#include "Jsondata.h"
#include "Keyset.h"
#include "LibraryModel.h"
#include "ReadingState.h"
#include "ReaderText.h"
#include "ThemePalette.h"
#include <cmath>
#include "cJSON.h"
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
    assert(cache.get_header()->cache_schema == CACHE_SCHEMA_VERSION && cache.get_header()->next_item_id == 4);
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
    assert(Encoded(Bytes(primary)).find("\"schema_version\":" + std::to_string(CACHE_SCHEMA_VERSION)) != std::string::npos);
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


static void OpaqueMigration()
{
    Reset();
    {
        Cache seed(L".cache.dat");assert(seed.init());
        auto book=seed.new_item(L"C:\\books\\keep.txt");int id=book->id;book->index=155;
        assert(seed.add_mark(book,45));assert(seed.set_mark_note(book,0,L"保留备注"));
        auto h=seed.get_header();h->item_id=id;h->font.lfHeight=-31;h->bg_color=0x182837;
        h->font_color=0xe0e1e2;h->line_gap=17;h->sidebar_width=275;h->reading_custom=1;
        assert(seed.save());
    }
    std::string normal=Encoded(Bytes(primary));cJSON* root=cJSON_Parse(normal.c_str());assert(root);
    cJSON* header=cJSON_GetObjectItem(root,"header");
    cJSON_SetNumberValue(cJSON_GetObjectItem(header,"alpha"),7);
    const unsigned originalStyle=0x00080000u|0x20u|0x10u|0x8u;
    cJSON_SetNumberValue(cJSON_GetObjectItem(header,"exstyle"),originalStyle);
    cJSON_SetNumberValue(cJSON_GetObjectItem(header,"fs_exstyle"),(originalStyle|0x100u));
    char* text=cJSON_PrintUnformatted(root);std::string original=Encoded(text);free(text);cJSON_Delete(root);
    for(const char* fault:{"backup","write","flush","replace",""})
    {
        Reset();Put(primary,original);Cache cache(L".cache.dat");assert(cache.init());auto h=cache.get_header();
        assert(h->alpha==255&&h->exstyle==0x18u&&h->fs_exstyle==0x118u);
        assert(h->font.lfHeight==-31&&h->bg_color==0x182837&&h->font_color==0xe0e1e2&&h->line_gap==17&&h->sidebar_width==275&&h->reading_custom==1);
        assert(h->item_count==1&&cache.get_item(0)->index==155&&cache.get_item(0)->mark[0]==45);
        assert(cache.mark_detail(cache.get_item(0),0).note==L"保留备注");
        TestFailure=fault;
        if(*fault){assert(!cache.save());assert(Bytes(primary)==original);TestFailure.clear();}
        assert(cache.save());assert(Bytes(L"C:\\Reader\\.cache.dat.v5.bak")==original);
        assert(Bytes(L"C:\\Reader\\.cache.dat.bak")==original);assert(cache.exit());
        Cache again(L".cache.dat");assert(again.init());assert(again.get_header()->alpha==255);
        assert(again.get_header()->exstyle==0x18u&&again.get_header()->fs_exstyle==0x118u);
        assert(again.get_item(0)->index==155&&again.mark_detail(again.get_item(0),0).note==L"保留备注");
        TestOperations.clear();assert(again.save()&&TestOperations.empty());
    }
}

static void Invalid()
{
    const std::string examples[] = {
        "{", "{}", R"({"schema_version":4,"header":{"item_count":0}})", R"({"schema_version":6,"header":{"item_count":0}})",
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
    assert(LibraryKey('A', true, true, false) == library_key_select_all);
    assert(LibraryKey('A', true, false, false) == library_key_none);
    assert(LibraryKey(46, false, true, false) == library_key_remove);
    assert(LibraryKey(46, false, false, false) == library_key_none); // Delete in the filter edits text only.
    assert(LibraryKey(13, false, true, false) == library_key_open);
    assert(LibraryKey(27, false, true, true) == library_key_cancel);
    assert(LibraryKey(27, false, true, false) == library_key_close);
    assert(LibraryKey(9, false, false, false) == library_key_none);
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
    assert(ReaderOpenResultIsCurrent(7, 7));
    assert(!ReaderOpenResultIsCurrent(8, 7));
    assert(!ReaderOpenResultIsCurrent(0, 7));
    assert(!ReaderOpenResultIsCurrent(0, 0));
    ReaderProgressObservation observed;
    assert(observed.MarkRead(1, 99, true, true));
    assert(!observed.MarkRead(1, 99, true, true)); // Repaint after an explicit unread action preserves that state.
    assert(observed.MarkRead(1, 100, true, true));
    assert(!observed.MarkRead(2, 100, false, true));
    assert(!observed.MarkRead(3, 0, true, true));
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
        assert(narrow.drawer && narrow.readingLeft == 48 * dpi / 96 && narrow.readingTop == 0);
        assert(narrow.sidebarWidth <= 300 * dpi / 96 && narrow.readingTop == 0);
        auto closed = ComputeReadingGeometry(1000 * dpi / 96, 600 * dpi / 96, dpi, true, false);
        auto wide = ComputeReadingGeometry(1000 * dpi / 96, 600 * dpi / 96, dpi, true, true);
        assert(!wide.drawer && wide.readingLeft == (48 * dpi / 96 + 240 * dpi / 96 + 6 * dpi / 96));
        assert(wide.readingRight == closed.readingRight && wide.readingTop == closed.readingTop);
        auto focus = ComputeReadingGeometry(300 * dpi / 96, 500 * dpi / 96, dpi, false, false);
        assert(focus.readingTop == 0 && focus.navigationWidth == 0 && focus.sidebarWidth == 0 && focus.readingLeft == 0);
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

static void BookmarkMetadata()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    int id = cache.new_item(L"C:\\book.web")->id;
    bookmark_detail_t first(2, 10); first.title = L"第二章"; first.summary = L"Chinese 中文 summary";
    assert(cache.add_mark(cache.get_by_id(id), 10, &first));
    assert(!cache.add_mark(cache.get_by_id(id), 10, &first));
    bookmark_detail_t second(4, 10); second.title = L"第四章"; second.summary = L"另一章节同一位置";
    assert(cache.add_mark(cache.get_by_id(id), 10, &second));
    assert(cache.add_mark(cache.get_by_id(id), 10)); // A raw legacy location is distinct.
    cache.rebase_marks(cache.get_by_id(id), 5, 100);
    assert(cache.get_by_id(id)->mark[0] == 10 && cache.get_by_id(id)->mark[1] == 10);
    assert(cache.get_by_id(id)->mark[2] == 110 && cache.mark_detail(cache.get_by_id(id), 2).chapter == -1);
    assert(cache.save());
    Cache restored(L".cache.dat"); assert(restored.init());
    auto item = restored.get_by_id(id);
    assert(restored.mark_detail(item, 0).title == L"第二章" && restored.mark_detail(item, 1).chapter == 4);
    assert(restored.mark_detail(item, 0).summary == first.summary);
    assert(restored.del_mark(item, 0));
    assert(item->mark_size == 2 && restored.mark_detail(item, 0).chapter == 4);
    assert(restored.save());
    Cache reloaded(L".cache.dat"); assert(reloaded.init());
    assert(reloaded.mark_detail(reloaded.get_by_id(id), 0).summary == second.summary);
    int other = reloaded.new_item(L"C:\\duplicate.web")->id;
    bookmark_detail_t moved(8, 0); moved.title = L"第八章"; moved.summary = L"开头";
    assert(reloaded.add_mark(reloaded.get_by_id(other), 0, &moved));
    assert(reloaded.relink_item(id, L"C:\\duplicate.web"));
    item = reloaded.get_by_id(id);
    assert(item->mark_size == 3 && reloaded.mark_detail(item, 2).chapter == 8);
    assert(reloaded.mark_detail(item, 2).summary == moved.summary);
    std::string version2 = Encoded(Bytes(primary));
    const std::string currentVersion = "\"schema_version\":" + std::to_string(CACHE_SCHEMA_VERSION);
    size_t versionAt = version2.find(currentVersion); assert(versionAt != std::string::npos);
    version2.replace(versionAt, currentVersion.size(), "\"schema_version\":2");
    Reset(); Put(primary, Encoded(version2));
    Cache previous(L".cache.dat"); assert(previous.init());
    assert(previous.get_by_id(id) && previous.mark_detail(previous.get_by_id(id), 0).chapter == 4);
    assert(previous.save() && Bytes(L"C:\\Reader\\.cache.dat.v2.bak") == Encoded(version2));
    Reset(); Put(primary, Encoded(Legacy()));
    Cache legacy(L".cache.dat"); assert(legacy.init());
    assert(legacy.mark_detail(legacy.get_by_id(1), 0).chapter == -1);
    assert(legacy.mark_detail(legacy.get_by_id(1), 0).summary.empty());
}

static void BookmarkInvalid()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    auto item = cache.new_item(L"C:\\book.web");
    bookmark_detail_t detail(3, 10); detail.title = L"title"; detail.summary = L"summary";
    assert(cache.add_mark(item, 10, &detail) && cache.save());
    const std::string saved = Encoded(Bytes(primary));
    for (int kind = 0; kind < 6; ++kind)
    {
        cJSON* root = cJSON_Parse(saved.c_str());
        cJSON* array = cJSON_GetObjectItem(root, "bookmark_details");
        cJSON* mark = cJSON_GetArrayItem(array, 0);
        if (kind == 0) cJSON_SetNumberValue(cJSON_GetObjectItem(mark, "position"), 11);
        if (kind == 1) cJSON_SetNumberValue(cJSON_GetObjectItem(mark, "mark_index"), 100);
        if (kind == 2) cJSON_SetNumberValue(cJSON_GetObjectItem(mark, "book_id"), 999);
        if (kind == 3) cJSON_SetNumberValue(cJSON_GetObjectItem(mark, "chapter"), -2);
        if (kind == 4) cJSON_AddItemToArray(array, cJSON_Duplicate(mark, TRUE));
        if (kind == 5) cJSON_ReplaceItemInObject(mark, "summary", cJSON_CreateString(std::string(250, 'x').c_str()));
        char* json = cJSON_PrintUnformatted(root); std::string damaged = Encoded(json); free(json); cJSON_Delete(root);
        Put(primary, damaged);
        Cache invalid(L".cache.dat"); assert(!invalid.init()); assert(Bytes(primary) == damaged);
    }
}

static void NotesAndPdfState()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    int id=cache.new_item(L"C:\\书籍.pdf")->id;
    item_t* item=cache.get_by_id(id);item->index=9;item->pdf_zoom=175;item->pdf_scroll=345678;item->pdf_x=12000;
    bookmark_detail_t detail(-1,5);detail.note=L"读书备注：中文 ✨";
    assert(cache.add_mark(item,5,&detail));assert(cache.set_mark_note(item,0,L"修订备注"));
    assert(!cache.set_mark_note(item,0,std::wstring(501,L'x')));assert(!cache.set_mark_note(item,-1,L"invalid"));
    assert(cache.save());std::string saved=Encoded(Bytes(primary));
    Cache restored(L".cache.dat");assert(restored.init());item=restored.get_by_id(id);
    assert(item->index==9&&item->pdf_zoom==175&&item->pdf_scroll==345678&&item->pdf_x==12000);
    assert(restored.mark_detail(item,0).note==L"修订备注");
    for (int kind=0;kind<4;++kind)
    {
        cJSON* root=cJSON_Parse(saved.c_str());cJSON* entry=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"items"),0);
        cJSON* mark=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"bookmark_details"),0);
        if(kind==0)cJSON_ReplaceItemInObject(mark,"note",cJSON_CreateString(std::string(501,'x').c_str()));
        if(kind==1)cJSON_ReplaceItemInObject(mark,"note",cJSON_CreateNumber(5));
        if(kind==2)cJSON_SetNumberValue(cJSON_GetObjectItem(entry,"pdf_scroll"),1000001);
        if(kind==3)cJSON_SetNumberValue(cJSON_GetObjectItem(entry,"pdf_zoom"),24);
        char* value=cJSON_PrintUnformatted(root);std::string broken=Encoded(value);free(value);cJSON_Delete(root);
        Put(primary,broken);Cache invalid(L".cache.dat");assert(!invalid.init()&&Bytes(primary)==broken);
    }
    // A v4 cache has no PDF state or notes, and receives an immutable migration backup.
    cJSON* root=cJSON_Parse(saved.c_str());cJSON_SetNumberValue(cJSON_GetObjectItem(root,"schema_version"),4);
    cJSON* entry=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"items"),0);
    for(const char* field:{"pdf_zoom","pdf_scroll","pdf_x"})cJSON_DeleteItemFromObject(entry,field);
    cJSON_DeleteItemFromObject(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"bookmark_details"),0),"note");
    char* value=cJSON_PrintUnformatted(root);std::string legacy=Encoded(value);free(value);cJSON_Delete(root);
    Put(primary,legacy);Cache old(L".cache.dat");assert(old.init());item=old.get_by_id(id);
    assert(item->pdf_zoom==0&&item->pdf_scroll==0&&old.mark_detail(item,0).note.empty());
    assert(old.save()&&Bytes(L"C:\\Reader\\.cache.dat.v4.bak")==legacy);
    // Duplicate-position notes are retained when relinking; oversized combinations fail safely.
    int duplicate=old.new_item(L"C:\\new.pdf")->id;
    assert(old.add_mark(old.get_by_id(duplicate),5,&detail));assert(old.set_mark_note(old.get_by_id(id),0,L"原书备注"));
    assert(old.relink_item(id,L"C:\\new.pdf"));
    assert(old.mark_detail(old.get_by_id(id),0).note.find(detail.note)!=std::wstring::npos);
    int conflict=old.new_item(L"C:\\conflict.pdf")->id;
    assert(old.add_mark(old.get_by_id(conflict),5,&detail));
    assert(old.set_mark_note(old.get_by_id(id),0,std::wstring(300,L'a')));
    assert(old.set_mark_note(old.get_by_id(conflict),0,std::wstring(300,L'b')));
    assert(!old.relink_item(id,L"C:\\conflict.pdf")&&old.get_by_id(conflict));
    assert(old.mark_detail(old.get_by_id(id),0).note==std::wstring(300,L'a'));

}

static void Batch()
{
    Reset(); Cache cache(L".cache.dat"); assert(cache.init());
    std::vector<int> ids;
    for (int i = 0; i < 10000; ++i) ids.push_back(cache.new_item((L"C:\\books\\" + std::to_wstring(i) + L".txt").c_str())->id);
    auto active = cache.get_by_id(ids[5]); active->index = 123; cache.add_mark(active, 17);
    assert(LibraryApplyState(*active, 1, 2));
    assert(!LibraryApplyState(*active, 1, 2));
    assert(LibraryApplyState(*active, -1, 0));
    assert(active->index == 123 && active->mark[0] == 17 && active->favorite == 1);
    assert(!LibraryApplyState(*active, -2, 7));
    std::vector<int> selected;
    for (size_t i = 0; i < ids.size(); i += 2) selected.push_back(ids[i]);
    selected.push_back(ids[0]); selected.push_back(-1); // A selection snapshot can contain a stale ID.
    cache.open_item(ids[5]);
    assert(cache.delete_items(selected) == 5000);
    assert(cache.get_by_id(ids[5])->index == 123 && cache.get_by_id(ids[5])->mark[0] == 17);
    assert(cache.get_header()->item_id == ids[5] && cache.get_header()->item_count == 5000);
    assert(cache.get_item(0)->id == ids[5]);
    assert(cache.delete_items(selected) == 0 && cache.get_header()->item_count == 5000);
    assert(TestFiles.empty()); // Removing original books never performs filesystem deletion.
    assert(cache.new_item(L"C:\\new.txt")->id > ids.back());
}


static void UiPersistence()
{
    Reset(); Cache source(L".cache.dat"); assert(source.init());
    assert(source.get_header()->ui_theme == 0 && source.get_header()->reading_custom == 0 && source.get_header()->sidebar_width == 240);
    int id = source.new_item(L"C:\\books\\preserve.epub")->id;
    source.get_by_id(id)->index = 401; source.add_mark(source.get_by_id(id), 17);
    source.get_header()->bg_color = 0x102030; source.get_header()->font_color = 0xF0E0D0;
    assert(source.save());
    cJSON* root = cJSON_Parse(Encoded(Bytes(primary)).c_str()); assert(root);
    cJSON_SetNumberValue(cJSON_GetObjectItem(root, "schema_version"), 3); cJSON_DeleteItemFromObject(root, "ui");
    char* json = cJSON_PrintUnformatted(root); std::string legacy = Encoded(json); free(json); cJSON_Delete(root);
    Put(primary, legacy);
    Cache migrated(L".cache.dat"); assert(migrated.init());
    auto header = migrated.get_header();
    assert(header->reading_custom == 1 && header->sidebar_width == 240 && header->ui_theme == 0);
    assert(header->bg_color == 0x102030 && header->font_color == 0xF0E0D0);
    assert(migrated.get_item(0)->id == id && migrated.get_item(0)->index == 401 && migrated.get_item(0)->mark[0] == 17);
    header->ui_theme = 1; header->reading_custom = 0; header->sidebar_width = 368;
    assert(migrated.save()); assert(Bytes(L"C:\\Reader\\.cache.dat.v3.bak") == legacy);
    Cache loaded(L".cache.dat"); assert(loaded.init());
    assert(loaded.get_header()->ui_theme == 1 && !loaded.get_header()->reading_custom && loaded.get_header()->sidebar_width == 368);
    assert(loaded.get_header()->bg_color == 0x102030 && loaded.get_header()->font_color == 0xF0E0D0);
    loaded.get_header()->sidebar_width = 444; TestFailure = "replace"; assert(!loaded.save());
    Cache unchanged(L".cache.dat"); assert(unchanged.init() && unchanged.get_header()->sidebar_width == 368);
    TestFailure.clear(); assert(loaded.save());
    assert(Bytes(L"C:\\Reader\\.cache.dat.v3.bak") == legacy);
}

static void UiInvalid()
{
    const char* values[] = { "null", "{}", "[]", "0", "{\"theme\":2,\"reading_custom\":0,\"sidebar_width\":240}",
        "{\"theme\":0.5,\"reading_custom\":0,\"sidebar_width\":240}", "{\"theme\":0,\"reading_custom\":2,\"sidebar_width\":240}",
        "{\"theme\":1,\"reading_custom\":1,\"sidebar_width\":159}", "{\"theme\":1,\"reading_custom\":1,\"sidebar_width\":641}",
        "{\"theme\":1,\"reading_custom\":1,\"sidebar_width\":240.5}" };
    for (auto ui : values)
    {
        Reset(); std::string original = Encoded(std::string("{\"schema_version\":4,\"header\":{\"item_count\":0},\"ui\":") + ui + "}");
        Put(primary, original); Cache broken(L".cache.dat"); assert(!broken.init());
        assert(Bytes(primary) == original && TestOperations.empty());
    }
}

static void Splitter()
{
    for (int dpi : {96, 120, 144, 168, 192})
    {
        int width = ReaderScale(1000, dpi);
        for (int logical : {160, 240, 368, 640})
        {
            auto g = ComputeReadingGeometry(width, ReaderScale(600, dpi), dpi, true, true, logical);
            assert(g.readingLeft == g.navigationWidth + g.sidebarWidth + g.splitterWidth && g.splitterWidth == ReaderScale(6, dpi));
            assert(g.sidebarWidth >= ReaderScale(160, dpi) && g.readingRight - g.readingLeft >= ReaderScale(320, dpi));
            ReaderSplitterDrag drag;
            drag.Begin(logical, g.sidebarWidth, g.sidebarWidth, dpi);
            assert(drag.Active());
            int next = drag.Move(g.sidebarWidth + ReaderScale(32, dpi), width-g.navigationWidth);
            assert(next >= 160 && next <= 640);
            assert(drag.Move(-100000, width-g.navigationWidth) == 160);
            assert(drag.Move(100000, width-g.navigationWidth) <= 640);
            assert(drag.Cancel() == logical && !drag.Active());
            assert(drag.Move(100000, width) == logical && !drag.Commit(logical));
            drag.Begin(logical, g.sidebarWidth, g.sidebarWidth, dpi); assert(!drag.Commit(logical));
            drag.Begin(logical, g.sidebarWidth, g.sidebarWidth, dpi); assert(drag.Commit(logical == 240 ? 320 : 240));
        }
        auto small = ComputeReadingGeometry(ReaderScale(700, dpi), ReaderScale(500, dpi), dpi, true, true, 640);
        assert(!small.drawer && small.readingRight - small.readingLeft >= ReaderScale(320, dpi));
        auto drawer = ComputeReadingGeometry(ReaderScale(699, dpi), ReaderScale(500, dpi), dpi, true, true, 640);
        assert(drawer.drawer && !drawer.splitterWidth && drawer.readingLeft == ReaderScale(48,dpi));
        auto focus = ComputeReadingGeometry(width, 600, dpi, false, false, 640);
        assert(!focus.sidebarWidth && !focus.splitterWidth && !focus.readingTop);
    }
}

static double Luminance(uint32_t rgb)
{
    double sum = 0; const double factors[] = {0.2126, 0.7152, 0.0722};
    for (int c = 0; c < 3; ++c) { double v = ((rgb >> (8 * c)) & 255) / 255.; sum += factors[c] * (v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4)); }
    return sum;
}
static double Contrast(uint32_t a, uint32_t b)
{ double x = Luminance(a), y = Luminance(b); return ((std::max)(x,y) + .05) / ((std::min)(x,y) + .05); }
static void Palette()
{
    for (int dark : {0,1})
    {
        ReaderPalette p = MakeReaderPalette(dark);
        for (uint32_t bg : {p.window, p.surface, p.raised, p.hover, p.pressed}) assert(Contrast(p.text, bg) >= 4.5);
        assert(Contrast(p.muted, p.window) >= 4.5 && Contrast(p.selectedText, p.selected) >= 4.5);
        assert(Contrast(p.hitText, p.hit) >= 4.5 && Contrast(p.accent, p.window) >= 3);
        auto follow = ResolveReaderColors(dark, false, 0x123456, 0x654321, 0xABCDEF, true);
        assert(follow.background == p.surface && follow.text == p.text && follow.title == p.text && !follow.image);
        auto custom = ResolveReaderColors(dark, true, 0x123456, 0x654321, 0xABCDEF, true);
        assert(custom.background == 0x123456 && custom.text == 0x654321 && custom.title == 0xABCDEF && custom.image);
    }
}

namespace Bridge {
static void ReaderNavigationState(BOOL, BOOL, BOOL, UINT = 0, BOOL = TRUE) {}
enum { book_text, book_online, book_web, book_pdf };
static int destroyed = 0, saves = 0;
struct Chapter {
    int index, size; std::wstring title;
    Chapter(int p, int length = 0, const std::wstring& name = L"") : index(p), size(length), title(name) {}
};
typedef std::vector<Chapter> chapters_t;
struct StubBook
{
    int* position = NULL;
    header_t* header = NULL;
    int type = book_text, chapter = 0, request = -1;
    bool loading = false, asynchronous = false;
    std::wstring text = std::wstring(1000, L'x'), filename;
    int highlight = -1, highlightLength = 0;
    std::vector<Chapter> chapters = { {0}, {100}, {300} };
    ~StubBook() { ++destroyed; }
    void Rebind(int* p, header_t* h) { position = p; header = h; }
    void SetChapterRule(chapter_rule_t*) {}
    void SetSearchHighlight(int p, int n) { highlight = p; highlightLength = n; }
    void SetFileName(const wchar_t* path) { filename = path; }
    const wchar_t* GetFileName() { return filename.c_str(); }
    wchar_t* GetText() { return &text[0]; }
    int GetTextLength() { return (int)text.size(); }
    void GetChapterTitle(wchar_t* out, int count) { wcsncpy(out, chapters[chapter].title.c_str(), count - 1); out[count - 1] = 0; }
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
struct PdfBook : StubBook
{
    void SaveView(item_t*) {} void RestoreView(const item_t*) {}
    BOOL SearchSnapshot(std::wstring& value) {value=text;return TRUE;}
    BOOL FindHit(int p,int n) {return p>=0&&n>0&&p<=(int)text.size()-n;}
};
static Cache _Cache(L".cache.dat");
static header_t* _header = NULL;
static item_t* _item = NULL;
static StubBook* _Book = NULL;
static int _activeBookId = -1;
static HWND _readerButtons[8] = {};
enum { IDM_VIEW=50001,IDM_MARK=50002 };
static BOOL _libraryVisible=FALSE,_tocVisible=FALSE;static HWND _hTreeMark=NULL;
static BOOL IsWindowVisible(HWND) {return FALSE;}
static ReadingHistory _readingHistory, _replayHistory;
static ReadingAnchor _jumpOrigin, _restoreAnchor;
static BOOL _jumpPending = FALSE, _restorePending = FALSE, _replaying = FALSE;
static unsigned long long _documentRevision = 1;
static HWND _hWnd = NULL;
static BOOL EC_IsEditMode() { return FALSE; }
static void EC_LeaveEditMode() {}
static BOOL EnableWindow(HWND, BOOL) { return TRUE; }
static void StopAutoPage(HWND) {}
static void StopLoadingImage(HWND) {}
static void OnUpdateChapters(HWND) {}
static void OnUpdateBookMark(HWND) {}
static void SaveLibrary() { ++saves; }
static void Save(HWND) { ++saves; }
static void LibraryChanged() {}
static void UpdateTitle(HWND) {}
static void Invalidate(HWND, BOOL, BOOL) {}
static void InvalidateReadingText() { ++_documentRevision; }
static void FinishReadingJump();
static void PrepareReadingJump(HWND);
#include "SearchDlg.h"
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

static void SearchBridge()
{
    Reset(); assert(_Cache.init());
    _activeBookId = _Cache.new_item(L"C:\\reading.txt")->id;
    StubBook book; _Book = &book; RebindReadingCache();
    book.text.replace(12, 3, L"hit"); book.text.replace(150, 3, L"hit"); book.text.replace(350, 3, L"hit");
    book.chapters = { {0, 100, L"第一章"}, {100, 200, L"第二章"}, {300, 700, L"第三章"} };
    reader_search_source_t source;
    assert(CaptureReaderSearch(&source) && source.base == 0 && source.text.size() == 1000 && source.token.chapter == -1);
    auto matches = ReaderFindAll(source.text, L"hit", true, []() { return false; }); assert(matches.count == 3);
    assert(ActivateReaderSearch(source.token, 12, 3) && _item->index == 12 && book.highlightLength == 3);
    book.type = book_online; book.chapter = 1;
    assert(CaptureReaderSearch(&source) && source.base == 100 && source.text.size() == 200 && source.token.chapter == 1);
    matches = ReaderFindAll(source.text, L"hit", true, []() { return false; }); assert(matches.count == 1 && matches.positions[0] == 50);
    assert(!ActivateReaderSearch(source.token, 12, 3)); // Never jump outside the stated current-chapter scope.
    assert(ActivateReaderSearch(source.token, 150, 3));
    book.chapter = -1; assert(!CaptureReaderSearch(&source)); book.chapter = 1;
    ReaderSearchToken old = source.token; ++_documentRevision;
    assert(!ActivateReaderSearch(old, 150, 3));
    assert(CaptureReaderSearch(&source)); old = source.token; book.chapter = 2;
    assert(!ActivateReaderSearch(old, 150, 3));
    book.loading = true; assert(!CaptureReaderSearch(&source)); book.loading = false;
    book.type = book_web; book.chapters = { {-1}, {-1}, {0, 1000, L"Web 当前章"} };
    assert(CaptureReaderSearch(&source) && source.token.chapter == 2 && source.base == 0);
    assert(source.scope.find(L"当前章") != std::wstring::npos);
    _Book = NULL;
}

static void BatchBridge()
{
    Reset(); assert(_Cache.init()); destroyed = 0;
    _activeBookId = _Cache.new_item(L"C:\\active.txt")->id;
    int other = _Cache.new_item(L"C:\\other.txt")->id;
    _Book = new StubBook; RebindReadingCache(); _item->index = 55;
    assert(RemoveLibraryBooks(std::vector<int>(1, other)) == 1 && destroyed == 0);
    assert(_Book->position == &_Cache.get_by_id(_activeBookId)->index && *_Book->position == 55);
    SetLibraryBooksState(std::vector<int>(1, _activeBookId), 1, 2);
    assert(_item->favorite == 1 && _item->read_state == 2 && _item->index == 55);
    std::vector<int> selected(1, _activeBookId);
    assert(RemoveLibraryBooks(selected) == 1 && !_Book && destroyed == 1);
    assert(RemoveLibraryBooks(selected) == 0 && destroyed == 1);
    int loading = _Cache.new_item(L"C:\\loading.epub")->id;
    _Book = new StubBook; _Book->loading = true; _Book->SetFileName(L"C:\\loading.epub");
    assert(RemoveLibraryBooks(std::vector<int>(1, loading)) == 1 && !_Book && destroyed == 2);
}
} // namespace Bridge

int main(int argc, char** argv)
{
    assert(argc == 2); std::string test = argv[1];
    if (test == "opacity") OpaqueMigration(); else if (test == "migration") Migration(); else if (test == "invalid") Invalid();
    else if (test == "identity") Identity(); else if (test == "dedup") Dedup();
    else if (test == "relink") Relink(); else if (test == "persistence") Persistence();
    else if (test == "filters") Filters(); else if (test == "navigation") Navigation();
    else if (test == "geometry") Geometry(); else if (test == "large") Large();
    else if (test == "import_bridge") Bridge::Import();
    else if (test == "navigation_bridge") Bridge::NavigationBridge();
    else if (test == "bookmark_metadata") BookmarkMetadata(); else if (test == "bookmark_invalid") BookmarkInvalid();
    else if (test == "notes_pdf") NotesAndPdfState();
    else if (test == "batch") Batch(); else if (test == "search_bridge") Bridge::SearchBridge();
    else if (test == "batch_bridge") Bridge::BatchBridge();
    else if (test == "ui_persistence") UiPersistence(); else if (test == "ui_invalid") UiInvalid();
    else if (test == "splitter") Splitter(); else if (test == "palette") Palette();
    else return 2;
    std::cout << test << " passed\n";
}
