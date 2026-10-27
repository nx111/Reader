#include "Theme.h"
#include "ReaderInteraction.h"
#include "SearchDlg.h"
#include "DPIAwareness.h"
#include <commctrl.h>
#include <process.h>
#include <memory>

namespace {
enum { query_id = 48001, run_id, cancel_id, case_id, results_id, previous_id, next_id, status_id, scope_id, search_timer = 93 };
HWND window = NULL, query = NULL, results = NULL, status = NULL, scope = NULL;
HFONT font = NULL;
UINT dpi = 96;
int Scale(int value) { return MulDiv(value, dpi, 96); }

struct SearchJob
{
    reader_search_source_t source;
    std::wstring query;
    bool matchCase;
    HANDLE cancel;
    LONG done;
    unsigned long request;
    ReaderSearchMatches matches;
    SearchJob() : matchCase(false), cancel(CreateEvent(NULL, TRUE, FALSE, NULL)), done(0) {}
    ~SearchJob() { if (cancel) CloseHandle(cancel); }
};
std::shared_ptr<SearchJob> active, completed;
ReaderSearchGate requests;

unsigned __stdcall SearchThread(void* argument)
{
    std::shared_ptr<SearchJob> task = *(std::shared_ptr<SearchJob>*)argument;
    delete (std::shared_ptr<SearchJob>*)argument;
    task->matches = ReaderFindAll(task->source.text, task->query, task->matchCase,
        [&task]() { return WaitForSingleObject(task->cancel, 0) == WAIT_OBJECT_0; });
    InterlockedExchange(&task->done, 1);
    return 0;
}

void Stop(bool clear)
{
    requests.Cancel();
    if (active) SetEvent(active->cancel);
    active.reset(); KillTimer(window, search_timer);
    EnableWindow(GetDlgItem(window, cancel_id), FALSE);
    if (clear)
    {
        completed.reset();
        ListView_SetItemCountEx(results, 0, 0);
        ClearReaderSearchHighlight();
    }
}

void Search()
{
    Stop(true);
    int length = GetWindowTextLength(query);
    if (length <= 0) { SetWindowText(status, L"请输入搜索词。按 Enter 开始搜索。"); SetFocus(query); return; }
    std::shared_ptr<SearchJob> task(new SearchJob);
    if (!task->cancel || !CaptureReaderSearch(&task->source))
    { SetWindowText(status, L"请先打开一本书，并等待正文加载完成。"); return; }
    std::vector<wchar_t> text(length + 1, 0);
    GetWindowText(query, &text[0], length + 1); task->query = &text[0];
    task->matchCase = SendMessage(GetDlgItem(window, case_id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    SetWindowText(scope, task->source.scope.c_str());
    task->request = requests.Begin(task->source.token);
    std::shared_ptr<SearchJob>* argument = new std::shared_ptr<SearchJob>(task);
    uintptr_t thread = _beginthreadex(NULL, 0, SearchThread, argument, 0, NULL);
    if (!thread) { delete argument; SetWindowText(status, L"无法启动搜索，请重试。"); return; }
    CloseHandle((HANDLE)thread); active = task;
    EnableWindow(GetDlgItem(window, cancel_id), TRUE);
    SetWindowText(status, L"正在搜索… 可取消；重新搜索会取代当前请求。");
    SetTimer(window, search_timer, 40, NULL);
}

ReaderExcerpt Excerpt(int row)
{
    if (!completed || row < 0 || row >= (int)completed->matches.positions.size()) return ReaderExcerpt();
    ReaderExcerpt excerpt = ReaderMakeExcerpt(completed->source.text,
        completed->matches.positions[row], completed->query.size());
    // Explicit delimiters also expose the hit to screen readers.
    excerpt.text.insert(excerpt.matchStart + excerpt.matchLength, L"】");
    excerpt.text.insert(excerpt.matchStart, L"【"); ++excerpt.matchStart;
    return excerpt;
}

void Activate()
{
    int row = ListView_GetNextItem(results, -1, LVNI_SELECTED);
    if (!completed || row < 0 || row >= (int)completed->matches.positions.size()) return;
    if (!ActivateReaderSearch(completed->source.token, completed->source.base + completed->matches.positions[row], (int)completed->query.size()))
    {
        Stop(true); SetWindowText(status, L"书籍或章节已改变，请重新搜索。");
    }
}

void Finish()
{
    if (!active || !InterlockedCompareExchange(&active->done, 0, 0)) return;
    if (!requests.Complete(active->request, active->source.token)) { Stop(true); return; }
    completed = active; active.reset(); KillTimer(window, search_timer);
    EnableWindow(GetDlgItem(window, cancel_id), FALSE);
    if (completed->matches.cancelled) { Stop(true); SetWindowText(status, L"搜索已取消。"); return; }
    ListView_SetItemCountEx(results, (int)completed->matches.positions.size(), 0);
    wchar_t text[160];
    if (!completed->matches.count) wcscpy(text, L"没有找到匹配内容。可修改搜索词或大小写选项后重试。");
    else if (completed->matches.count > completed->matches.positions.size())
        _stprintf(text, L"共 %llu 条结果，仅显示前 %u 条；双击或 Enter 跳转。", (unsigned long long)completed->matches.count, (unsigned)completed->matches.positions.size());
    else _stprintf(text, L"共 %llu 条结果；双击或 Enter 跳转，F3／Shift+F3 前后选择。", (unsigned long long)completed->matches.count);
    SetWindowText(status, text);
    if (!completed->matches.positions.empty()) ListView_SetItemState(results, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void Layout()
{
    RECT rc; GetClientRect(window, &rc);
    const int gap = Scale(8), height = Scale(28), button = Scale(76);
    MoveWindow(GetDlgItem(window, 48020), gap, gap, Scale(62), height, TRUE);
    MoveWindow(query, Scale(75), gap, (std::max)(25, (int)rc.right - Scale(75) - button * 2 - gap * 3), height, TRUE);
    MoveWindow(GetDlgItem(window, run_id), rc.right - button * 2 - gap * 2, gap, button, height, TRUE);
    MoveWindow(GetDlgItem(window, cancel_id), rc.right - button - gap, gap, button, height, TRUE);
    MoveWindow(GetDlgItem(window, case_id), gap, gap * 2 + height, Scale(120), height, TRUE);
    MoveWindow(GetDlgItem(window, previous_id), rc.right - button * 2 - gap * 2, gap * 2 + height, button, height, TRUE);
    MoveWindow(GetDlgItem(window, next_id), rc.right - button - gap, gap * 2 + height, button, height, TRUE);
    MoveWindow(scope, gap, gap * 3 + height * 2, (std::max)(1, (int)rc.right - gap * 2), height, TRUE);
    int top = gap * 4 + height * 3;
    MoveWindow(results, gap, top, (std::max)(1, (int)rc.right - gap * 2), (std::max)(1, (int)rc.bottom - top - height * 2 - gap * 2), TRUE);
    MoveWindow(status, gap, (std::max)(top, (int)rc.bottom - height * 2 - gap), (std::max)(1, (int)rc.right - gap * 2), height * 2, TRUE);
}

LRESULT DrawHit(NMLVCUSTOMDRAW* draw)
{
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT)) return CDRF_NOTIFYSUBITEMDRAW;
    if (draw->nmcd.dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM) || draw->iSubItem != 1) return CDRF_DODEFAULT;
    int row = (int)draw->nmcd.dwItemSpec;
    ReaderExcerpt excerpt = Excerpt(row);
    RECT rc; ListView_GetSubItemRect(results, row, 1, LVIR_BOUNDS, &rc);
    bool selected = (ListView_GetItemState(results, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HDC dc = draw->nmcd.hdc; int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    HBRUSH background = CreateSolidBrush(selected ? ThemeColors().selected : ThemeColors().surface);
    FillRect(dc, &rc, background); DeleteObject(background);
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, font);
    int x = rc.left + ReaderItemInset(rc.right-rc.left,dpi);
    std::wstring parts[] = { excerpt.text.substr(0, excerpt.matchStart), excerpt.text.substr(excerpt.matchStart, excerpt.matchLength), excerpt.text.substr(excerpt.matchStart + excerpt.matchLength) };
    for (int i = 0; i < 3; ++i)
    {
        SIZE size = {}; GetTextExtentPoint32(dc, parts[i].c_str(), (int)parts[i].size(), &size);
        if (i == 1)
        {
            RECT hit = { x, rc.top, x + size.cx, rc.bottom };
            HBRUSH brush = CreateSolidBrush(ThemeColors().hit); FillRect(dc, &hit, brush); DeleteObject(brush);
        }
        SetTextColor(dc, i == 1 ? ThemeColors().hitText : selected ? ThemeColors().selectedText : ThemeColors().text);
        TextOut(dc, x, rc.top + ((rc.bottom - rc.top - size.cy) / 2), parts[i].c_str(), (int)parts[i].size());
        x += size.cx;
    }
    if(GetFocus()==results && (ListView_GetItemState(results,row,LVIS_FOCUSED)&LVIS_FOCUSED)) {RECT cue=rc;InflateRect(&cue,-1,-1);DrawFocusRect(dc,&cue);}
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

LRESULT CALLBACK SearchProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            window = handle; dpi = GetWindowDpiCompat(handle);
            NONCLIENTMETRICS metrics = {}; metrics.cbSize = sizeof(metrics);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
            metrics.lfMessageFont.lfHeight = -MulDiv(9, dpi, 72); metrics.lfMessageFont.lfWidth = 0;
            font = CreateFontIndirect(&metrics.lfMessageFont);
            CreateWindow(L"STATIC", L"搜索词(&F)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, handle, (HMENU)48020, NULL, NULL);
            query = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, handle, (HMENU)query_id, NULL, NULL);
            SendMessage(query, EM_SETLIMITTEXT, 256, 0);
            SendMessage(query, EM_SETCUEBANNER, FALSE, (LPARAM)L"在当前范围内查找文字");
            const wchar_t* labels[] = { L"搜索", L"取消搜索", L"区分大小写", L"上一条", L"下一条" };
            const int ids[] = { run_id, cancel_id, case_id, previous_id, next_id };
            for (int i = 0; i < 5; ++i) CreateWindow(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 2 ? BS_AUTOCHECKBOX : BS_PUSHBUTTON), 0, 0, 0, 0, handle, (HMENU)(INT_PTR)ids[i], NULL, NULL);
            scope = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, handle, (HMENU)scope_id, NULL, NULL);
            results = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"搜索结果", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, handle, (HMENU)results_id, NULL, NULL);
            SetProp(results,L"ReaderPaddedList",(HANDLE)1);
            ListView_SetExtendedListViewStyle(results, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            const wchar_t* titles[] = { L"章节", L"匹配与上下文", L"字符位置" };
            const int widths[] = { 140, 390, 90 };
            for (int i = 0; i < 3; ++i) { LVCOLUMN column = {}; column.mask = LVCF_WIDTH | LVCF_TEXT; column.cx = Scale(widths[i]); column.pszText = (wchar_t*)titles[i]; ListView_InsertColumn(results, i, &column); }
            status = CreateWindow(L"STATIC", L"输入搜索词后按 Enter。", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, handle, (HMENU)status_id, NULL, NULL);
            for (HWND child = GetWindow(handle, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
            EnableWindow(GetDlgItem(handle, cancel_id), FALSE); Layout(); ThemeAttachWindow(handle);
        } return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = Scale(380); ((MINMAXINFO*)lParam)->ptMinTrackSize.y = Scale(350); return 0;
    case WM_TIMER: if (wParam == search_timer) Finish(); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == run_id) Search();
        else if (LOWORD(wParam) == cancel_id) { Stop(true); SetWindowText(status, L"搜索已取消。可重新搜索。"); }
        else if (LOWORD(wParam) == previous_id || LOWORD(wParam) == next_id) AdvanceReaderSearch(LOWORD(wParam) == previous_id);
        else if ((LOWORD(wParam) == query_id && HIWORD(wParam) == EN_CHANGE) || LOWORD(wParam) == case_id)
        { Stop(true); SetWindowText(status, L"按 Enter 搜索；修改条件不会自动跳转正文。"); }
        return 0;
    case WM_NOTIFY:
        {
            NMHDR* header = (NMHDR*)lParam;
            if (header->hwndFrom != results) break;
            if (header->code == NM_CUSTOMDRAW) return DrawHit((NMLVCUSTOMDRAW*)lParam);
            if (header->code == NM_DBLCLK) Activate();
            if (header->code == LVN_GETDISPINFO && completed)
            {
                NMLVDISPINFO* info = (NMLVDISPINFO*)lParam;
                int row = info->item.iItem;
                if (!(info->item.mask & LVIF_TEXT) || row < 0 || row >= (int)completed->matches.positions.size()) return 0;
                int position = completed->matches.positions[row];
                std::wstring text;
                if (info->item.iSubItem == 0)
                { int chapter = ReaderChapterAt(completed->source.chapters, position); text = chapter >= 0 ? completed->source.chapters[chapter].title : L"正文"; }
                else if (info->item.iSubItem == 1) text = Excerpt(row).text;
                else text = std::to_wstring(completed->source.base + position + 1);
                lstrcpyn(info->item.pszText, text.c_str(), info->item.cchTextMax);
            }
        } return 0;
    case 0x02E0:
        {
            UINT previousDpi = dpi; dpi = LOWORD(wParam);
            for (int i = 0; i < 3; ++i) ListView_SetColumnWidth(results, i, MulDiv(ListView_GetColumnWidth(results, i), dpi, previousDpi));
            LOGFONT next = {}; GetObject(font, sizeof(next), &next); next.lfHeight = -MulDiv(9, dpi, 72);
            HFONT old = font; font = CreateFontIndirect(&next);
            for (HWND child = GetWindow(handle, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
            DeleteObject(old); RECT* r = (RECT*)lParam;
            SetWindowPos(handle, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); Layout();
        } return 0;
    case WM_CLOSE: Stop(true); ShowWindow(handle, SW_HIDE); return 0;
    case WM_DESTROY: Stop(false); completed.reset(); DeleteObject(font); font = NULL; window = query = results = status = scope = NULL; return 0;
    }
    return DefWindowProc(handle, message, wParam, lParam);
}
} // namespace

void OpenReaderSearch(HWND owner)
{
    if (!window)
    {
        dpi = GetWindowDpiCompat(owner);
        WNDCLASS type = {}; type.lpfnWndProc = SearchProc; type.hInstance = GetModuleHandle(NULL); type.lpszClassName = L"ReaderSearchResults";
        type.hCursor = LoadCursor(NULL, IDC_ARROW); type.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&type);
        window = CreateWindowEx(WS_EX_CONTROLPARENT, type.lpszClassName, L"正文搜索结果", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(700), Scale(450), owner, NULL, type.hInstance, NULL);
    }
    SetWindowText(scope, ReaderSearchScope().c_str()); ShowWindow(window, SW_SHOWNORMAL); SetForegroundWindow(window); SetFocus(query); SendMessage(query, EM_SETSEL, 0, -1);
}

void InvalidateReaderSearch()
{
    if (!window) return;
    Stop(true); SetWindowText(scope, ReaderSearchScope().c_str()); SetWindowText(status, L"书籍或当前章节已改变，请重新搜索。");
}
void CloseReaderSearch() { if (window) DestroyWindow(window); }
void AdvanceReaderSearch(BOOL previous)
{
    if (!completed || completed->matches.positions.empty()) return;
    int row = ListView_GetNextItem(results, -1, LVNI_SELECTED), count = (int)completed->matches.positions.size();
    row = row < 0 ? (previous ? count - 1 : 0) : (row + (previous ? count - 1 : 1)) % count;
    ListView_SetItemState(results, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(results, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(results, row, FALSE); Activate();
}
BOOL ReaderSearchTranslate(MSG* message)
{
    if (!window || !IsWindowVisible(window) || (message->hwnd != window && !IsChild(window, message->hwnd))) return FALSE;
    if (message->message == WM_KEYDOWN)
    {
        wchar_t type[32] = {}; GetClassName(message->hwnd, type, 32);
        ReaderSearchCommand command = ReaderSearchKey((unsigned)message->wParam,
            (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
            message->hwnd == results, _wcsicmp(type, L"BUTTON") == 0, active != NULL);
        switch (command)
        {
        case search_key_cancel: Stop(true); SetWindowText(status, L"搜索已取消。"); return TRUE;
        case search_key_close: SendMessage(window, WM_CLOSE, 0, 0); return TRUE;
        case search_key_activate: Activate(); return TRUE;
        case search_key_run: Search(); return TRUE;
        case search_key_click: SendMessage(message->hwnd, BM_CLICK, 0, 0); return TRUE;
        case search_key_previous: AdvanceReaderSearch(TRUE); return TRUE;
        case search_key_next: AdvanceReaderSearch(FALSE); return TRUE;
        case search_key_focus: SetFocus(query); SendMessage(query, EM_SETSEL, 0, -1); return TRUE;
        default: break;
        }
    }
    return IsDialogMessage(window, message);
}
