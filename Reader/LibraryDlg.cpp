#include "Theme.h"
#include "NavigationLayout.h"
#include "PdfEngine.h"
#include "PdfBook.h"
#include "DrawReaderIcon.h"
#include "LibraryDlg.h"
#include "LibraryModel.h"
#include "ReaderText.h"
#include "DPIAwareness.h"
#include "Utils.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <process.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <map>
#include <set>
#include <time.h>
#include "unzip.h"
#include "iowin32.h"
#include "libxml/parser.h"
#include "libxml/xpath.h"
#include "mobi.h"

namespace {
enum { search_id = 47001, filter_id, add_id, folder_id, open_id, relink_id,
    favorite_id, read_id, remove_id, refresh_id, stop_id, list_id, status_id,
    nav_all_id=47101, nav_reading_id, nav_read_id, nav_favorite_id, nav_missing_id,
    import_timer = 91, filter_timer = 92 };
HWND library = NULL, list = NULL, search = NULL, filter = NULL, status = NULL;
HWND sideTitle = NULL, sideButtons[5] = {}, tooltips = NULL;
HFONT headingFont = NULL, titleFont = NULL;
HIMAGELIST rowSpacing = NULL;
ReaderLibraryGeometry layout = {};
Cache* cache = NULL;
std::vector<int> rows;
std::map<int, bool> missing;
int sortColumn = -1;
bool ascending = true;
bool updatingSelection = false;
HFONT uiFont = NULL;
UINT libraryDpi = 96;
int Scale(int value) { return MulDiv(value, libraryDpi, 96); }

struct ImportJob
{
    HANDLE cancel, slots;
    CRITICAL_SECTION lock;
    std::deque<item_t> ready;
    std::vector<std::wstring> roots;
    std::vector<std::pair<int, std::wstring> > checks;
    LONG done, skipped;
    bool checkOnly;
    ImportJob() : done(0), skipped(0), checkOnly(false)
    {
        cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
        slots = CreateSemaphore(NULL, 128, 128, NULL);
        InitializeCriticalSection(&lock);
    }
    ~ImportJob() { CloseHandle(cancel); CloseHandle(slots); DeleteCriticalSection(&lock); }
    bool Cancelled() { return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; }
    bool Push(const item_t& item)
    {
        HANDLE handles[] = { cancel, slots };
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return false;
        EnterCriticalSection(&lock); ready.push_back(item); LeaveCriticalSection(&lock);
        return true;
    }
};
std::shared_ptr<ImportJob> job;
size_t imported = 0;

void CopyMetadata(wchar_t* target, const char* value)
{
    if (!value || !*value) return;
    int length = 0;
    wchar_t* wide = utf8_to_utf16(value, (int)strlen(value), &length);
    if (wide) { std::wstring clipped = ReaderClipText(wide, 255); wcscpy(target, clipped.c_str()); free_buffer(wide); }
}

bool ZipText(unzFile zip, const char* name, std::string& out)
{
    unz_file_info64 info = {};
    if (unzLocateFile(zip, name, 1) != UNZ_OK
        || unzGetCurrentFileInfo64(zip, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK
        || !info.uncompressed_size || info.uncompressed_size > 2 * 1024 * 1024
        || unzOpenCurrentFile(zip) != UNZ_OK) return false;
    out.resize((size_t)info.uncompressed_size);
    size_t offset = 0;
    while (offset < out.size())
    {
        int n = unzReadCurrentFile(zip, &out[offset], (unsigned)(out.size() - offset));
        if (n <= 0) break;
        offset += n;
    }
    int result = unzCloseCurrentFile(zip);
    return offset == out.size() && result == UNZ_OK;
}

// Metadata is optional. No full text parsing, unpacking or copying into the library.
void ReadMetadata(item_t& item)
{
    const TCHAR* ext = PathFindExtension(item.file_name);
    if (_tcsicmp(ext, _T(".txt")) == 0) return;
    if (_tcsicmp(ext, L".pdf") == 0)
    {
        ReaderPdfDocument pdf;
        if(pdf.Open(item.file_name,ReaderPdfLibraryPath()))
        { if(!pdf.title.empty()) lstrcpyn(item.title,pdf.title.c_str(),256); if(!pdf.author.empty()) lstrcpyn(item.author,pdf.author.c_str(),256);item.text_length=pdf.pages; }
        return;
    }
    if (_tcsicmp(ext, _T(".epub")) == 0)
    {
        zlib_filefunc64_def functions = {};
        fill_win32_filefunc64W(&functions);
        unzFile zip = unzOpen2_64(item.file_name, &functions);
        if (!zip) return;
        std::string container, opf;
        if (ZipText(zip, "META-INF/container.xml", container))
        {
            xmlDocPtr doc = xmlReadMemory(container.data(), (int)container.size(), NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
            xmlXPathContextPtr context = doc ? xmlXPathNewContext(doc) : NULL;
            xmlXPathObjectPtr path = context ? xmlXPathEvalExpression(BAD_CAST "//*[local-name()='rootfile']/@full-path", context) : NULL;
            xmlChar* value = path && path->nodesetval && path->nodesetval->nodeNr ? xmlNodeGetContent(path->nodesetval->nodeTab[0]) : NULL;
            if (value) ZipText(zip, (const char*)value, opf);
            xmlFree(value);
            if (path) xmlXPathFreeObject(path);
            if (context) xmlXPathFreeContext(context);
            if (doc) xmlFreeDoc(doc);
        }
        unzClose(zip);
        if (opf.empty()) return;
        xmlDocPtr doc = xmlReadMemory(opf.data(), (int)opf.size(), NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        xmlXPathContextPtr context = doc ? xmlXPathNewContext(doc) : NULL;
        const char* queries[] = { "//*[local-name()='metadata']/*[local-name()='title']", "//*[local-name()='metadata']/*[local-name()='creator']" };
        for (int i = 0; context && i < 2; ++i)
        {
            xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST queries[i], context);
            xmlChar* value = result && result->nodesetval && result->nodesetval->nodeNr ? xmlNodeGetContent(result->nodesetval->nodeTab[0]) : NULL;
            if (value) CopyMetadata(i == 0 ? item.title : item.author, (const char*)value);
            xmlFree(value);
            if (result) xmlXPathFreeObject(result);
        }
        if (context) xmlXPathFreeContext(context);
        if (doc) xmlFreeDoc(doc);
    }
    else if (_tcsicmp(ext, _T(".ol")) && _tcsicmp(ext, _T(".web")))
    {
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        // libmobi's metadata reader retains the source records in memory.
        if (!GetFileAttributesEx(item.file_name, GetFileExInfoStandard, &attrs)
            || attrs.nFileSizeHigh || attrs.nFileSizeLow > 64 * 1024 * 1024) return;
        FILE* file = _tfopen(item.file_name, _T("rb"));
        MOBIData* mobi = file ? mobi_init() : NULL;
        if (mobi && mobi_load_file(mobi, file) == MOBI_SUCCESS)
        {
            char* title = mobi_meta_get_title(mobi);
            char* author = mobi_meta_get_author(mobi);
            CopyMetadata(item.title, title); CopyMetadata(item.author, author);
            free(title); free(author);
        }
        if (mobi) mobi_free(mobi);
        if (file) fclose(file);
    }
}

void IndexPath(ImportJob& task, const std::wstring& path)
{
    if (!IsLibraryBookFile(path.c_str()) || path.size() >= MAX_PATH) { InterlockedIncrement(&task.skipped); return; }
    item_t item = {};
    if (!Cache::canonical_path(path.c_str(), item.file_name)) { InterlockedIncrement(&task.skipped); return; }
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attrs)
        || (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (!attrs.nFileSizeLow && !attrs.nFileSizeHigh))
    { InterlockedIncrement(&task.skipped); return; }
    Cache::file_key(item.file_name, item.file_key);
    _tcsncpy(item.title, PathFindFileName(item.file_name), 255); PathRemoveExtension(item.title);
    ReadMetadata(item);
    if (!task.Cancelled()) task.Push(item);
}

unsigned __stdcall ImportThread(void* argument)
{
    std::shared_ptr<ImportJob> task = *(std::shared_ptr<ImportJob>*)argument;
    delete (std::shared_ptr<ImportJob>*)argument;
    if (task->checkOnly)
    {
        for (size_t i = 0; i < task->checks.size() && !task->Cancelled(); ++i)
        {
            item_t item = {}; item.id = task->checks[i].first;
            DWORD attrs = GetFileAttributes(task->checks[i].second.c_str());
            item.is_new = attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY);
            if (!task->Push(item)) break;
        }
    }
    else
    {
        std::vector<std::wstring> pending = task->roots;
        while (!pending.empty() && !task->Cancelled())
        {
            std::wstring path = pending.back(); pending.pop_back();
            DWORD attrs = GetFileAttributes(path.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) { InterlockedIncrement(&task->skipped); continue; }
            if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) { IndexPath(*task, path); continue; }
            if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) { InterlockedIncrement(&task->skipped); continue; }
            WIN32_FIND_DATA found;
            HANDLE enumeration = FindFirstFile((path + L"\\*").c_str(), &found);
            if (enumeration == INVALID_HANDLE_VALUE) { InterlockedIncrement(&task->skipped); continue; }
            do
            {
                if (task->Cancelled()) break;
                if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
                std::wstring child = path + L"\\" + found.cFileName;
                if (child.size() >= MAX_PATH) { InterlockedIncrement(&task->skipped); continue; }
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pending.push_back(child);
                }
                else IndexPath(*task, child);
            } while (FindNextFile(enumeration, &found));
            FindClose(enumeration);
        }
    }
    InterlockedExchange(&task->done, 1);
    return 0;
}

std::vector<int> SelectedIds()
{
    std::vector<int> selected;
    int row = -1;
    while ((row = ListView_GetNextItem(list, row, LVNI_SELECTED)) >= 0)
        if (row < (int)rows.size()) selected.push_back(rows[row]);
    return selected;
}

int SelectedId()
{
    if (ListView_GetSelectedCount(list) != 1) return -1;
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    return row >= 0 && row < (int)rows.size() ? rows[row] : -1;
}

void SelectionStatus()
{
    if (!cache || !list || job || updatingSelection) return;
    wchar_t text[180];
    _stprintf(text, L"%u / %d 本 · 已选 %u 本 · Ctrl+A 全选；移出不会删除文件", (unsigned)rows.size(), cache->get_header()->item_count, ListView_GetSelectedCount(list));
    SetWindowText(status, text);
}

int StateMenu(bool favorite)
{
    HMENU menu = CreatePopupMenu();
    if (favorite)
    { AppendMenu(menu, MF_STRING, 1, L"设为收藏"); AppendMenu(menu, MF_STRING, 2, L"取消收藏"); }
    else
    { AppendMenu(menu, MF_STRING, 1, L"标为未读（保留进度）"); AppendMenu(menu, MF_STRING, 2, L"标为在读"); AppendMenu(menu, MF_STRING, 3, L"标为已读"); }
    RECT bounds; GetWindowRect(GetDlgItem(library, favorite ? favorite_id : read_id), &bounds);
    int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, bounds.left, bounds.bottom, 0, library, NULL);
    DestroyMenu(menu);
    return action;
}

void RefreshRows()
{
    if (!cache || !list) return;
    std::vector<int> selectedIds = SelectedIds();
    std::set<int> selected(selectedIds.begin(), selectedIds.end());
    int focusRow = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
    int focused = focusRow >= 0 && focusRow < (int)rows.size() ? rows[focusRow] : -1;
    wchar_t query[256] = {};
    GetWindowText(search, query, 256);
    int mode = (int)SendMessage(filter, CB_GETCURSEL, 0, 0);
    rows.clear();
    for (int i = 0; i < cache->get_header()->item_count; ++i)
    {
        const item_t* item = cache->get_item(i);
        if (LibraryMatches(*item, missing[item->id], mode, query)) rows.push_back(item->id);
    }
    if (sortColumn >= 0)
    {
        std::stable_sort(rows.begin(), rows.end(), [](int a, int b)
        {
            const item_t* x = cache->get_by_id(a); const item_t* y = cache->get_by_id(b);
            int value = LibraryCompare(*x, *y, missing[a], missing[b], sortColumn);
            return ascending ? value < 0 : value > 0;
        });
    }
    updatingSelection = true;
    ListView_SetItemCountEx(list, (int)rows.size(), LVSICF_NOINVALIDATEALL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UINT state = (selected.count(rows[i]) ? LVIS_SELECTED : 0) | (rows[i] == focused ? LVIS_FOCUSED : 0);
        if (state) ListView_SetItemState(list, (int)i, state, LVIS_SELECTED | LVIS_FOCUSED);
    }
    updatingSelection = false;
    InvalidateRect(list, NULL, FALSE);
    SelectionStatus();
}

void SetBusy(bool busy)
{
    const int controls[] = { add_id, folder_id, relink_id, refresh_id, remove_id };
    for (size_t i = 0; i < sizeof(controls) / sizeof(*controls); ++i) EnableWindow(GetDlgItem(library, controls[i]), !busy);
    EnableWindow(GetDlgItem(library, stop_id), busy);
}

void CancelImport()
{
    if (!job) return;
    bool checkOnly = job->checkOnly;
    SetEvent(job->cancel); job.reset(); KillTimer(library, import_timer); SetBusy(false);
    if (!checkOnly) SaveLibrary();
    RefreshRows(); SetWindowText(status, L"已停止；已入库的记录保留，未处理的队列已丢弃。");
}

void StartJob(const std::shared_ptr<ImportJob>& task)
{
    if (job) return;
    job = task; imported = 0;
    std::shared_ptr<ImportJob>* argument = new std::shared_ptr<ImportJob>(task);
    uintptr_t thread = _beginthreadex(NULL, 0, ImportThread, argument, 0, NULL);
    if (!thread)
    {
        delete argument; job.reset();
        MessageBox(library, L"无法启动导入线程。书库记录未改变。", L"本地书库", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle((HANDLE)thread);
    SetBusy(true); SetTimer(library, import_timer, 40, NULL);
}

void CheckFiles()
{
    if (job) return;
    std::shared_ptr<ImportJob> task(new ImportJob);
    task->checkOnly = true;
    for (int i = 0; i < cache->get_header()->item_count; ++i)
    { const item_t* item = cache->get_item(i); task->checks.push_back(std::make_pair(item->id, item->file_name)); }
    StartJob(task);
}

void DrainImport()
{
    if (!job) return;
    if (!job->checkOnly && (!IsWindowEnabled(library) || !LibraryCanAcceptEntries())) return;
    std::vector<item_t> batch;
    EnterCriticalSection(&job->lock);
    while (!job->ready.empty() && batch.size() < 24)
    { batch.push_back(job->ready.front()); job->ready.pop_front(); }
    bool empty = job->ready.empty();
    LeaveCriticalSection(&job->lock);
    if (!batch.empty())
    {
        ReleaseSemaphore(job->slots, (LONG)batch.size(), NULL);
        if (job->checkOnly)
            for (size_t i = 0; i < batch.size(); ++i) missing[batch[i].id] = batch[i].is_new != 0;
        else
        {
            ImportLibraryEntries(batch);
            for (size_t i = 0; i < batch.size(); ++i)
            { item_t* item = cache->find_item(batch[i].file_name, batch[i].file_key); if (item) missing[item->id] = false; }
        }
        imported += batch.size();
    }
    // Refresh at completion or every ~250 entries, not once per imported book.
    if (empty && InterlockedCompareExchange(&job->done, 0, 0))
    {
        bool checkOnly = job->checkOnly;
        LONG skipped = job->skipped;
        bool cancelled = job->Cancelled();
        KillTimer(library, import_timer); job.reset(); SetBusy(false);
        if (!checkOnly) SaveLibrary();
        RefreshRows();
        if (!checkOnly)
        {
            wchar_t text[180];
            _stprintf(text, L"%s：已处理 %u 个文件，跳过 %ld 项；相同路径或文件身份自动去重", cancelled ? L"已停止" : L"导入完成", (unsigned)imported, skipped);
            SetWindowText(status, text);
        }
    }
    else
    {
        wchar_t text[128]; _stprintf(text, job->checkOnly ? L"正在检查文件… %u" : L"正在索引… %u（可停止）", (unsigned)imported);
        SetWindowText(status, text);
        if (!batch.empty() && imported % 240 == 0) RefreshRows();
    }
}

std::vector<std::wstring> PickFiles(bool multi)
{
    std::vector<wchar_t> buffer(multi ? 65536 : MAX_PATH, 0);
    OPENFILENAME dialog = {};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = library;
    dialog.lpstrFilter = L"电子书\0*.txt;*.epub;*.pdf;*.mobi;*.azw3;*.azw;*.prc;*.pdb\0所有文件\0*.*\0\0";
    dialog.lpstrFile = &buffer[0]; dialog.nMaxFile = (DWORD)buffer.size();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (multi ? OFN_ALLOWMULTISELECT : 0);
    std::vector<std::wstring> result;
    if (!GetOpenFileName(&dialog)) return result;
    const wchar_t* part = &buffer[0] + wcslen(&buffer[0]) + 1;
    if (!multi || !*part) result.push_back(&buffer[0]);
    else for (; *part; part += wcslen(part) + 1) result.push_back(std::wstring(&buffer[0]) + L"\\" + part);
    return result;
}

void ActivateSelected()
{
    int id = SelectedId();
    if (id < 0) return;
    if (missing[id])
    { MessageBox(library, L"原文件已失联。进度和书签仍保留，请使用“重新关联”。", L"本地书库", MB_OK | MB_ICONINFORMATION); return; }
    OpenLibraryBook(id);
    HWND owner=GetAncestor(library,GA_ROOT);SetForegroundWindow(owner);SetFocus(owner);
}

void SyncFilters()
{
    int selected=(int)SendMessage(filter,CB_GETCURSEL,0,0);
    for(int i=0;i<5;++i) SendMessage(sideButtons[i],BM_SETCHECK,i==selected?BST_CHECKED:BST_UNCHECKED,0);
}
void UpdateVisualMetrics()
{
    if(headingFont) DeleteObject(headingFont); if(titleFont) DeleteObject(titleFont);
    LOGFONT font={}; GetObject(uiFont,sizeof(font),&font); font.lfWeight=FW_SEMIBOLD;
    titleFont=CreateFontIndirect(&font); font.lfHeight=-MulDiv(12,libraryDpi,72); headingFont=CreateFontIndirect(&font);
    SendMessage(sideTitle,WM_SETFONT,(WPARAM)headingFont,TRUE); SetProp(list,L"ReaderTitleFont",titleFont);
    HIMAGELIST previous=rowSpacing; rowSpacing=ImageList_Create(1,Scale(38),ILC_COLOR32,1,1);
    ListView_SetImageList(list,rowSpacing,LVSIL_SMALL); if(previous) ImageList_Destroy(previous);
}
void Layout()
{
    RECT rc; GetClientRect(library,&rc); layout=ReaderLibraryLayout(rc.right,rc.bottom,libraryDpi,false);
    auto show=[](HWND child,bool visible) { if((IsWindowVisible(child)!=FALSE)!=visible) ShowWindow(child,visible?SW_SHOWNA:SW_HIDE); };
    auto move=[](HWND child,int x,int y,int w,int h) { RECT old; GetWindowRect(child,&old); MapWindowPoints(NULL,library,(POINT*)&old,2);
        if(old.left!=x || old.top!=y || old.right-old.left!=w || old.bottom-old.top!=h) SetWindowPos(child,NULL,x,y,(std::max)(1,w),(std::max)(1,h),SWP_NOACTIVATE|SWP_NOZORDER); };
    move(sideTitle,Scale(20),Scale(20),layout.sidebarWidth-Scale(32),Scale(24));
    for(int i=0;i<5;++i)
    {
        move(sideButtons[i],Scale(8),Scale(68+i*42),layout.sidebarWidth-Scale(16),Scale(38));
        show(sideButtons[i],layout.sidebar);
    }
    show(sideTitle,layout.sidebar); show(filter,!layout.sidebar);
    move(search,layout.left+Scale(30),layout.searchTop+Scale(5),layout.searchWidth-Scale(38),Scale(22));
    if(!layout.sidebar) move(filter,layout.filterLeft,layout.searchTop,layout.filterWidth,Scale(240));
    const int buttons[]={add_id,folder_id,open_id,relink_id,favorite_id,read_id,remove_id,refresh_id,stop_id};
    for(int i=0;i<9;++i) move(GetDlgItem(library,buttons[i]),layout.left+(i%layout.actionColumns)*(layout.actionSize+layout.actionGap),layout.actionsTop+(i/layout.actionColumns)*(layout.actionSize+layout.actionGap),layout.actionSize,layout.actionSize);
    move(list,layout.left,layout.listTop,layout.width,layout.listHeight);
    move(status,layout.left,layout.statusTop,layout.width,Scale(24));
    InvalidateRect(library,NULL,FALSE);
}

LRESULT CALLBACK LibraryProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            library = window;
            libraryDpi = GetWindowDpiCompat(window);
            NONCLIENTMETRICS metrics = {}; metrics.cbSize = sizeof(metrics);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
            metrics.lfMessageFont.lfHeight = -MulDiv(9, libraryDpi, 72); metrics.lfMessageFont.lfWidth = 0;
            uiFont = CreateFontIndirect(&metrics.lfMessageFont);
            search = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window, (HMENU)search_id, NULL, NULL);
            SendMessage(search, EM_SETCUEBANNER, FALSE, (LPARAM)L"搜索书名、作者或路径");
            filter = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, window, (HMENU)filter_id, NULL, NULL);
            const wchar_t* filters[] = { L"全部", L"在读", L"已读", L"收藏", L"失联" };
            for (int i = 0; i < 5; ++i) SendMessage(filter, CB_ADDSTRING, 0, (LPARAM)filters[i]);
            SendMessage(filter, CB_SETCURSEL, 0, 0);
            const wchar_t* labels[] = { L"添加文件", L"添加文件夹", L"继续阅读", L"重新关联", L"收藏操作", L"阅读状态", L"移出书库", L"刷新", L"停止" };
            const int ids[] = { add_id, folder_id, open_id, relink_id, favorite_id, read_id, remove_id, refresh_id, stop_id };
            for (int i = 0; i < 9; ++i) CreateWindow(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, (HMENU)(INT_PTR)ids[i], NULL, NULL);
            list = CreateWindowEx(0, WC_LISTVIEW, L"本地书库", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0, 0, 0, 0, window, (HMENU)list_id, NULL, NULL);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            const wchar_t* titles[] = { L"书名", L"作者", L"格式", L"进度", L"最近阅读", L"状态", L"原文件" };
            const int widths[] = { 205, 120, 65, 110, 145, 100, 350 };
            for (int i = 0; i < 7; ++i)
            { LVCOLUMN col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM; col.pszText = (wchar_t*)titles[i]; col.cx = Scale(widths[i]); col.iSubItem = i; ListView_InsertColumn(list, i, &col); }
            status = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, (HMENU)status_id, NULL, NULL);
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessage(child, WM_SETFONT, (WPARAM)uiFont, TRUE);
            sideTitle=CreateWindow(L"STATIC",L"我的书库",WS_CHILD|SS_LEFT,0,0,1,1,window,NULL,NULL,NULL);
            const wchar_t* navigation[]={L"全部藏书",L"正在阅读",L"已读",L"收藏",L"失联记录"};
            const ReaderIcon icons[]={ReaderIcon::Library,ReaderIcon::Reading,ReaderIcon::Read,ReaderIcon::Favorite,ReaderIcon::Missing};
            for(int i=0;i<5;++i)
            {
                sideButtons[i]=CreateWindow(L"BUTTON",navigation[i],WS_CHILD|WS_TABSTOP|BS_RADIOBUTTON|BS_PUSHLIKE|(i==0?WS_GROUP:0),0,0,1,1,window,(HMENU)(INT_PTR)(nav_all_id+i),NULL,NULL);
                SendMessage(sideButtons[i],WM_SETFONT,(WPARAM)uiFont,FALSE); SetProp(sideButtons[i],L"ReaderSidebarItem",(HANDLE)1); SetProp(sideButtons[i],L"ReaderIcon",(HANDLE)(INT_PTR)icons[i]);
            }
            tooltips=CreateWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,NULL,NULL,NULL);
            const ReaderIcon actions[]={ReaderIcon::AddFile,ReaderIcon::Folder,ReaderIcon::Open,ReaderIcon::Relink,ReaderIcon::Favorite,ReaderIcon::ReadState,ReaderIcon::Remove,ReaderIcon::Refresh,ReaderIcon::Stop};
            for(int i=0;i<9;++i)
            {
                HWND button=GetDlgItem(window,ids[i]); SetProp(button,L"ReaderFlatIcon",(HANDLE)1); SetProp(button,L"ReaderIcon",(HANDLE)(INT_PTR)actions[i]);
                TOOLINFO tip={}; tip.cbSize=sizeof(tip); tip.hwnd=window; tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS; tip.uId=(UINT_PTR)button; tip.lpszText=(wchar_t*)labels[i];
                SendMessage(tooltips,TTM_ADDTOOL,0,(LPARAM)&tip);
            }
            SetProp(status,L"ReaderMutedText",(HANDLE)1); SetProp(list,L"ReaderLibraryList",(HANDLE)1); SyncFilters(); UpdateVisualMetrics();
            SetBusy(false); Layout(); RefreshRows(); ThemeAttachWindow(window); ThemeAttachWindow(tooltips);
        } return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint); FillRect(dc,&paint.rcPaint,ThemeWindowBrush());
            RECT searchBox={layout.left,layout.searchTop,layout.left+layout.searchWidth,layout.searchTop+Scale(32)};
            HBRUSH surface=CreateSolidBrush(ThemeColors().surface); FillRect(dc,&searchBox,surface); DeleteObject(surface);
            HBRUSH edge=CreateSolidBrush(GetFocus()==search?ThemeColors().accent:ThemeColors().border); FrameRect(dc,&searchBox,edge);
            RECT glyph={searchBox.left+Scale(8),searchBox.top+Scale(8),searchBox.left+Scale(24),searchBox.top+Scale(24)};
            DrawReaderIcon(dc,ReaderIcon::Search,glyph,ThemeColors().muted);
            if(layout.sidebar) { RECT line={layout.sidebarWidth-1,Scale(16),layout.sidebarWidth,paint.rcPaint.bottom}; FillRect(dc,&line,edge); }
            DeleteObject(edge); EndPaint(window,&paint);
        } return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = Scale(330);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = Scale(360); return 0;
    case 0x02E0: // WM_DPICHANGED
        {
            UINT oldDpi = libraryDpi; libraryDpi = LOWORD(wParam);
            for (int i = 0; i < 7; ++i) ListView_SetColumnWidth(list, i, MulDiv(ListView_GetColumnWidth(list, i), libraryDpi, oldDpi));
            HFONT previous = uiFont;
            LOGFONT font = {}; GetObject(previous, sizeof(font), &font);
            font.lfHeight = -MulDiv(9, libraryDpi, 72); font.lfWidth = 0;
            uiFont = CreateFontIndirect(&font);
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessage(child, WM_SETFONT, (WPARAM)uiFont, TRUE);
            if (previous) DeleteObject(previous);
            RECT* bounds = (RECT*)lParam;
            SetWindowPos(window, NULL, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateVisualMetrics(); Layout();
        } return 0;
    case WM_CLOSE: CloseLibraryView(); return 0;
    case WM_TIMER:
        if (wParam == import_timer) DrainImport();
        if (wParam == filter_timer) { KillTimer(window, filter_timer); RefreshRows(); }
        return 0;
    case WM_COMMAND:
        {
            int command = LOWORD(wParam), notification = HIWORD(wParam);
            if(command>=nav_all_id && command<=nav_missing_id)
            { SendMessage(filter,CB_SETCURSEL,command-nav_all_id,0); SyncFilters(); RefreshRows(); return 0; }
            if(command==filter_id && notification==CBN_SELCHANGE) SyncFilters();
            int id = SelectedId(); item_t* item = cache->get_by_id(id);
            std::vector<int> selected;
            if (command == remove_id || command == favorite_id || command == read_id) selected = SelectedIds();
            if ((command == search_id && notification == EN_CHANGE) || (command == filter_id && notification == CBN_SELCHANGE))
                SetTimer(window, filter_timer, 150, NULL);
            else if (command == add_id && !job)
            { std::shared_ptr<ImportJob> task(new ImportJob); task->roots = PickFiles(true); if (!task->roots.empty()) StartJob(task); }
            else if (command == folder_id && !job)
            {
                BROWSEINFO browse = {}; browse.hwndOwner = window; browse.lpszTitle = L"索引文件夹及子文件夹中的原文件"; browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST selected = SHBrowseForFolder(&browse); wchar_t path[MAX_PATH];
                if (selected && SHGetPathFromIDList(selected, path))
                { std::shared_ptr<ImportJob> task(new ImportJob); task->roots.push_back(path); StartJob(task); }
                CoTaskMemFree(selected);
            }
            else if (command == stop_id && job) CancelImport();
            else if (command == refresh_id && !job) CheckFiles();
            else if ((command == open_id || command == relink_id) && id < 0)
                SetWindowText(status, L"继续阅读或重新关联时，请只选择一本书。");
            else if (command == open_id) ActivateSelected();
            else if (command == remove_id && !job)
            {
                if (selected.empty()) { SetWindowText(status, L"请先选择要移出的书籍。"); return 0; }
                wchar_t question[180];
                _stprintf(question, L"从书库移出选中的 %u 本书？将移除这些阅读记录和书签；磁盘原文件会保留。", (unsigned)selected.size());
                if (MessageBox(window, question, L"批量移出书库", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                {
                    RemoveLibraryBooks(selected);
                    for (size_t i = 0; i < selected.size(); ++i) missing.erase(selected[i]);
                    RefreshRows();
                }
            }
            else if (command == relink_id && item && !job)
            {
                // Explicit relinking handles one selected original, preserving the source identity.
                std::vector<std::wstring> files = PickFiles(false);
                if (!files.empty() && IsLibraryBookFile(files[0].c_str()))
                {
                    wchar_t key[64]; Cache::file_key(files[0].c_str(), key);
                    item_t* duplicate = cache->find_item(files[0].c_str(), key);
                    if (duplicate && duplicate->id != id && MessageBox(window, L"此文件已在书库中。合并为当前记录，保留当前进度，并将另一进度作为书签保存？", L"重新关联", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                    if (RelinkLibraryBook(id, files[0].c_str(), key)) { missing[id] = false; RefreshRows(); }
                    else MessageBox(window, L"无法关联。文件须存在；合并不能丢弃超出容量的书签。记录仍保留。", L"本地书库", MB_OK | MB_ICONERROR);
                }
            }
            else if (command == favorite_id || command == read_id)
            {
                if (selected.empty()) { SetWindowText(status, L"请先选择书籍，可用 Ctrl／Shift 多选。"); return 0; }
                int action = StateMenu(command == favorite_id);
                if (action)
                {
                    SetLibraryBooksState(selected, command == favorite_id ? (action == 1 ? 1 : 0) : -1, command == read_id ? action - 1 : -1);
                    RefreshRows();
                }
            }
        } return 0;
    case WM_NOTIFY:
        {
            NMHDR* header = (NMHDR*)lParam;
            if (header->hwndFrom != list) break;
            if (header->code == LVN_GETDISPINFO)
            {
                NMLVDISPINFO* info = (NMLVDISPINFO*)lParam;
                if (info->item.iItem < 0 || info->item.iItem >= (int)rows.size()) return 0;
                item_t* item = cache->get_by_id(rows[info->item.iItem]);
                if (!item || !(info->item.mask & LVIF_TEXT)) return 0;
                wchar_t text[512] = {};
                switch (info->item.iSubItem)
                {
                case 0: _sntprintf(text, 511, L"%s%s", item->favorite ? L"★ " : L"", item->title); break;
                case 1: _tcsncpy(text, item->author[0] ? item->author : L"—", 511); break;
                case 2: { const wchar_t* extension = PathFindExtension(item->file_name); _tcsncpy(text, *extension ? extension + 1 : L"—", 511); CharUpperBuff(text, (DWORD)wcslen(text)); } break;
                case 3:
                    if (item->read_state == 2) wcscpy(text, L"已读");
                    else if (item->text_length && !_tcsicmp(PathFindExtension(item->file_name),L".pdf"))
                    {
                        if (!item->last_read && !item->index && !item->read_state) _stprintf(text,L"未读 · %d 页",item->text_length);
                        else _stprintf(text,L"第 %d / %d 页",(std::min)(item->text_length,item->index+1),item->text_length);
                    }
                    else if (item->text_length)
                    {
                        const wchar_t* ext = PathFindExtension(item->file_name);
                        const wchar_t* scope = !_tcsicmp(ext, L".web") ? L"本章 " : !_tcsicmp(ext, L".ol") ? L"已加载 " : L"";
                        _stprintf(text, L"%s%.1f%%", scope, (std::min)(100.0, 100.0 * item->index / item->text_length));
                    }
                    else if (item->index) _stprintf(text, L"位置 %d", item->index);
                    else wcscpy(text, L"未读");
                    break;
                case 4:
                    if (!item->last_read) wcscpy(text, L"—");
                    else
                    {
                        ULARGE_INTEGER ticks; ticks.QuadPart = item->last_read * 10000000ULL + 116444736000000000ULL;
                        FILETIME utc = { ticks.LowPart, ticks.HighPart }, local; SYSTEMTIME date;
                        if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &date))
                            _stprintf(text, L"%04u-%02u-%02u %02u:%02u", date.wYear, date.wMonth, date.wDay, date.wHour, date.wMinute);
                    } break;
                case 5: wcscpy(text, missing[item->id] ? L"失联" : item->read_state == 2 ? L"已读" : item->read_state == 1 ? L"在读" : L"未读"); break;
                default: wcscpy(text, item->file_name); break;
                }
                lstrcpyn(info->item.pszText, text, info->item.cchTextMax);
            }
            else if (header->code == LVN_COLUMNCLICK)
            { int column = ((NMLISTVIEW*)lParam)->iSubItem; ascending = sortColumn == column ? !ascending : column != 4; sortColumn = column; RefreshRows(); }
            else if (header->code == LVN_ITEMCHANGED || header->code == LVN_ODSTATECHANGED) SelectionStatus();
            else if (header->code == NM_DBLCLK) ActivateSelected();
            else if (header->code == LVN_KEYDOWN)
            {
                WORD key = ((NMLVKEYDOWN*)lParam)->wVKey;
                if (key == VK_RETURN) ActivateSelected();
                if (key == VK_DELETE) SendMessage(window, WM_COMMAND, remove_id, 0);
            }
        } return 0;
    case WM_DESTROY:
        if (job) SetEvent(job->cancel);
        job.reset(); library = list = search = filter = status = NULL;
        if(rowSpacing) ImageList_Destroy(rowSpacing); rowSpacing=NULL;
        if(headingFont) DeleteObject(headingFont); headingFont=NULL;
        if(titleFont) DeleteObject(titleFont); titleFont=NULL;
        if(tooltips) DestroyWindow(tooltips); tooltips=NULL;
        if (uiFont) DeleteObject(uiFont); uiFont = NULL;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}
} // namespace

void OpenLibrary(HWND owner, Cache& data)
{
    cache = &data;
    if (!library)
    {
        libraryDpi = GetWindowDpiCompat(owner);
        xmlInitParser();
        WNDCLASS type = {}; type.lpfnWndProc = LibraryProc; type.hInstance = GetModuleHandle(NULL);
        type.lpszClassName = L"ReaderLocalLibrary"; type.hCursor = LoadCursor(NULL, IDC_ARROW); type.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClass(&type);
        library = CreateWindowEx(WS_EX_CONTROLPARENT,type.lpszClassName,L"本地书库",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,1,1,owner,NULL,type.hInstance,NULL);
    }
    SendMessage(owner,WM_READER_LIBRARY_VIEW,TRUE,0);SetForegroundWindow(owner);
    RefreshRows();CheckFiles();SetFocus(search);
}

void LibraryChanged() { if (library) SetTimer(library, filter_timer, 150, NULL); }
void CloseLibrary() { if(library) {HWND owner=GetParent(library);DestroyWindow(library);if(IsWindow(owner)) SendMessage(owner,WM_READER_LIBRARY_VIEW,FALSE,0);} }
void CloseLibraryView() {if(library) SendMessage(GetParent(library),WM_READER_LIBRARY_VIEW,FALSE,0);}
void LayoutLibraryView(HWND owner,const RECT* rect,UINT dpi,BOOL visible)
{
    if(!library || GetParent(library)!=owner) return;
    if(dpi!=libraryDpi) SendMessage(library,0x02e0,MAKELONG(dpi,dpi),(LPARAM)rect);
    else SetWindowPos(library,HWND_TOP,rect->left,rect->top,(std::max)(1,(int)(rect->right-rect->left)),(std::max)(1,(int)(rect->bottom-rect->top)),SWP_NOACTIVATE);
    if((IsWindowVisible(library)!=FALSE)!=(visible!=FALSE)) ShowWindow(library,visible?SW_SHOWNA:SW_HIDE);
}
BOOL LibraryTranslateMessage(MSG* message)
{
    if (!library || !IsWindowVisible(library) || (message->hwnd != library && !IsChild(library, message->hwnd))) return FALSE;
    if (message->message == WM_KEYDOWN)
    {
        LibraryKeyCommand command = LibraryKey((unsigned)message->wParam,
            (GetKeyState(VK_CONTROL) & 0x8000) != 0, message->hwnd == list, job != NULL);
        switch (command)
        {
        case library_key_cancel: CancelImport(); return TRUE;
        case library_key_close: CloseLibraryView(); return TRUE;
        case library_key_focus: SetFocus(search); return TRUE;
        case library_key_select_all:
            updatingSelection = true; ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
            updatingSelection = false; SelectionStatus(); return TRUE;
        case library_key_open: ActivateSelected(); return TRUE;
        case library_key_remove: if (!job) SendMessage(library, WM_COMMAND, remove_id, 0); return TRUE;
        default: break;
        }
    }
    return IsDialogMessage(library, message);
}
