// Reader.cpp : Defines the entry point for the application.
//

#include "Reader.h"
#include "TextBook.h"
#include "EpubBook.h"
#include "MobiBook.h"
#include "OnlineBook.h"
#include "Keyset.h"
#include "Editctrl.h"
#include "Advset.h"
#include "DPIAwareness.h"
#include "barcode.h"
#include "OnlineDlg.h"
#include "WebDlg.h"
#include "WebBook.h"
#include "DisplaySet.h"
#include "LibraryDlg.h"
#include "ReadingState.h"
#include "SearchDlg.h"
#include "LibraryModel.h"
#include "Theme.h"
#include "GdiSurface.h"
#include <time.h>
#if ENABLE_TAG
#include "tagset.h"
#endif
#include <stdio.h>
#include <shlwapi.h>
#include <CommDlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <vector>
#ifdef _DEBUG
#include "dump.h"
#endif

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "shell32.lib")

#define MIN_ALPHA_VALUE             0x64

#define MAX_LOADSTRING              256

// Global Variables:
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                    // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
TCHAR szStatusClass[MAX_LOADSTRING];            // the status window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Setting(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Proxy(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    JumpProgress(HWND, UINT, WPARAM, LPARAM);
#ifdef ENABLE_NETWORK
INT_PTR CALLBACK    UpgradeProc(HWND, UINT, WPARAM, LPARAM);
#endif

static void _free_resource(HWND hWnd);
static void _update_data(HWND hWnd, BOOL keep_header, BOOL do_save);
static void UpdateReaderLayout(HWND);
static void SyncCurrentChapter(BOOL reveal = FALSE);
static void ToggleToc(HWND);
static void CloseToc(HWND);
static void NavigatePosition(HWND, int);
static BOOL BeginReadingJump();
static void PrepareReadingJump(HWND);
static void FinishReadingJump();
static void NavigateHistory(HWND, BOOL);
static BOOL ReaderShortcut(MSG*);
static void NavigateBookmark(HWND, int);
void InvalidateReadingText();
static unsigned long long _documentRevision = 1;
static void RebindReadingCache();
static LRESULT CALLBACK TocProc(HWND, UINT, WPARAM, LPARAM);
static HWND _readerButtons[8] = {};
static HFONT _readerUIFont = NULL;
static WNDPROC _tocProc = NULL;
static BOOL _tocFromFocus = FALSE;
static BOOL _focusMode = FALSE, _tocVisible = FALSE, _syncingToc = FALSE, _routingWheel = FALSE;
static RECT _readingRect = {};
static ReadingGeometry _readingGeometry = {};
static std::vector<HTREEITEM> _chapterItems;
static int _tocCurrent = -1, _activeBookId = -1;
static ReadingHistory _readingHistory, _replayHistory;
static ReadingAnchor _jumpOrigin, _restoreAnchor;
static BOOL _jumpPending = FALSE, _replaying = FALSE, _restorePending = FALSE;
#define WM_TOC_ACTIVATE (WM_USER + 107)


static const UINT WM_READER_UPDATE_MENU = WM_APP + 731;
static HWND _hReadingView = NULL, _hSplitter = NULL;
static WNDPROC _splitterProc = NULL, _markProc = NULL;
static ReaderSplitterDrag _splitterDrag;
static int _sidebarPreviewWidth = 240;
static GdiSurface _readingSurface, _frameSurface;
static UINT _chapterBookToken = 0, _markBookToken = 0;
static std::vector<std::wstring> _chapterTitles, _markLabels;
static LRESULT CALLBACK ReadingProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK SplitterProc(HWND, UINT, WPARAM, LPARAM);
static void CancelSplitter(HWND);

static void PositionControl(HWND child, HWND after, int x, int y, int w, int h)
{
    RECT previous; GetWindowRect(child, &previous); MapWindowPoints(NULL, GetParent(child), (POINT*)&previous, 2);
    if (previous.left != x || previous.top != y || previous.right - previous.left != w || previous.bottom - previous.top != h)
        SetWindowPos(child, after, x, y, w, h, SWP_NOACTIVATE | (after ? 0 : SWP_NOZORDER));
}
static void VisibleControl(HWND child, bool visible)
{ if ((IsWindowVisible(child) != FALSE) != visible) ShowWindow(child, visible ? SW_SHOWNA : SW_HIDE); }

static void DrawLoading(HDC dc, const RECT& rc)
{
    if (!_loading || !_loading->enable || !_loading->image || rc.right <= rc.left || rc.bottom <= rc.top) return;
    double scale = (std::min)((double)(rc.right - rc.left) / _loading->image->GetWidth(), (double)(rc.bottom - rc.top) / _loading->image->GetHeight());
    scale = (std::min)(1.0, scale);
    int width = (int)(_loading->image->GetWidth() * scale), height = (int)(_loading->image->GetHeight() * scale);
    Gdiplus::Graphics graphics(dc);
    graphics.DrawImage(_loading->image, (rc.right - width) / 2, (rc.bottom - height) / 2, width, height);
}

static LRESULT CALLBACK ReadingProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND owner = GetParent(window);
    switch (message)
    {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
            OnPaint(owner, dc); EndPaint(window, &paint);
        } return 0;
    case WM_PRINTCLIENT: return OnPaint(owner, (HDC)wParam);
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        return SendMessage(owner, message, wParam, lParam);
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
        {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            MapWindowPoints(window, owner, &point, 1);
            return SendMessage(owner, message, wParam, MAKELPARAM(point.x, point.y));
        }
    case WM_NCHITTEST:
        if (_WndInfo.status == ds_borderless && SendMessage(owner, message, wParam, lParam) != HTCLIENT) return HTTRANSPARENT;
        break;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

static void CancelSplitter(HWND owner)
{
    if (!_splitterDrag.Active()) return;
    _header->sidebar_width = _splitterDrag.Cancel();
    if (GetCapture() == _hSplitter) ReleaseCapture();
    UpdateReaderLayout(owner); Invalidate(owner, TRUE, FALSE); ResumeAutoPage(owner);
}
static LRESULT CALLBACK SplitterProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND owner = GetParent(window);
    switch (message)
    {
    case WM_SETCURSOR: SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN:
        {
            POINT point; GetCursorPos(&point); ScreenToClient(owner, &point);
            if (_readingGeometry.drawer) return 0;
            SetFocus(window); SetCapture(window);
            if (GetCapture() != window) return 0;
            _sidebarPreviewWidth = _header->sidebar_width;
            _splitterDrag.Begin(_header->sidebar_width, _readingGeometry.sidebarWidth, point.x, GetWindowDpiCompat(owner));
            PauseAutoPage(owner);
        } return 0;
    case WM_MOUSEMOVE:
        if (_splitterDrag.Active() && GetCapture() == window)
        {
            POINT point; GetCursorPos(&point); ScreenToClient(owner, &point);
            RECT client; GetClientRect(owner, &client);
            int width = _splitterDrag.Move(point.x, client.right);
            if (width != _sidebarPreviewWidth) { _sidebarPreviewWidth = width; UpdateReaderLayout(owner); Invalidate(owner, TRUE, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (_splitterDrag.Active())
        {
            _header->sidebar_width = _sidebarPreviewWidth;
            bool changed = _splitterDrag.Commit(_header->sidebar_width);
            if (GetCapture() == window) ReleaseCapture();
            ResumeAutoPage(owner); if (changed) Save(owner);
        } return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED: CancelSplitter(owner); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && _splitterDrag.Active()) { CancelSplitter(owner); return 0; }
        if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_HOME)
        {
            CancelSplitter(owner);
            int width = wParam == VK_HOME ? 240 : _header->sidebar_width + (wParam == VK_LEFT ? -8 : 8);
            RECT rc; GetClientRect(owner, &rc); int dpi = GetWindowDpiCompat(owner);
            _header->sidebar_width = ReaderSidebarWidth(rc.right, dpi, width) * 96 / dpi;
            UpdateReaderLayout(owner); Invalidate(owner, TRUE, FALSE); Save(owner); return 0;
        }
        break;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_SETFOCUS: InvalidateRect(window, NULL, FALSE); break;
    case WM_KILLFOCUS: CancelSplitter(owner); InvalidateRect(window, NULL, FALSE); break;
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); RECT rc; GetClientRect(window, &rc);
            HBRUSH brush = CreateSolidBrush(GetFocus() == window || _splitterDrag.Active() ? ThemeColors().accent : ThemeColors().border);
            FillRect(dc, &rc, brush); DeleteObject(brush); EndPaint(window, &paint);
        } return 0;
    }
    return CallWindowProc(_splitterProc, window, message, wParam, lParam);
}

void ApplyReaderTheme()
{
    CancelSplitter(_hWnd); ThemeConfigure(_header);
    if (_Book) _Book->ResetLayout();
    Invalidate(_hWnd, FALSE, FALSE);
}

// Only containers supported by libmobi: MOBI/KF8 and PalmDoc text (not KFX,
// Topaz, Print Replica or arbitrary Palm databases/programs).
static BOOL IsMobiBookExtension(const TCHAR *ext)
{
    if (ext == NULL || *ext == 0)
        return FALSE;

    return _tcsicmp(ext, _T(".mobi")) == 0
        || _tcsicmp(ext, _T(".azw3")) == 0
        || _tcsicmp(ext, _T(".azw")) == 0
        || _tcsicmp(ext, _T(".prc")) == 0
        || _tcsicmp(ext, _T(".pdb")) == 0;
}

// Keep file validation and drag-and-drop in sync, including networkless builds.
static BOOL IsSupportedBookExtension(const TCHAR *ext)
{
    if (ext == NULL || *ext == 0)
        return FALSE;

    return _tcsicmp(ext, _T(".txt")) == 0
        || _tcsicmp(ext, _T(".epub")) == 0
        || IsMobiBookExtension(ext)
#ifdef ENABLE_NETWORK
        || _tcsicmp(ext, _T(".ol")) == 0
        || _tcsicmp(ext, _T(".web")) == 0
#endif
        ;
}


int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

     // TODO: Place code here.
#ifdef _DEBUG
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif
    MSG msg;
    HACCEL hAccelTable;
    HANDLE hMutex;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_READER, szWindowClass, MAX_LOADSTRING);
    LoadString(hInstance, IDC_STATUSBAR, szStatusClass, MAX_LOADSTRING);

    // check if application is already running
    hMutex = OpenMutex(MUTEX_ALL_ACCESS, TRUE, szTitle);
    if (hMutex == NULL && ERROR_FILE_NOT_FOUND == GetLastError())
    {
        hMutex = CreateMutex(NULL, TRUE, szTitle);
    }
    else
    {
        // There's another instance running.
        HWND hWnd = NULL;
        LPWSTR *argv;
        int argc;
        COPYDATASTRUCT copyData;

        EnumWindows(EnumWindowsProc, (LPARAM)&hWnd);

        // open new file
        if (hWnd)
        {
            argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argc > 1)
            {
                // open input argument file name
                copyData.dwData = 0;
                copyData.cbData = (DWORD)(wcslen(argv[1]) + 1) * sizeof(wchar_t);
                copyData.lpData = argv[1];
                SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM)&copyData);
            }
        }
        return 0;
    }

    // init com
    CoInitialize(NULL);

    // init gdiplus
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // load cache file
    if (!Init())
    {
        return FALSE;
    }

    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_READER));

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (ReaderSearchTranslate(&msg) || LibraryTranslateMessage(&msg) || ReaderShortcut(&msg)) continue;
        if (!_hFindDlg || !IsDialogMessage(_hFindDlg, &msg))
        {
            if (!TranslateAccelerator(_hWnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    Exit();

    // uninit gdiplus
    Gdiplus::GdiplusShutdown(gdiplusToken);

    // uninit com
    CoUninitialize();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return (int) msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW /*| CS_DBLCLKS*/;
    wcex.lpfnWndProc      = WndProc;
    wcex.cbClsExtra       = 0;
    wcex.cbWndExtra       = 0;
    wcex.hInstance        = hInstance;
    wcex.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BOOK));
    wcex.hCursor          = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = NULL; // Reading and chrome supply their own buffered backgrounds.
    wcex.lpszMenuName     = MAKEINTRESOURCE(IDC_READER);
    wcex.lpszClassName    = szWindowClass;
    wcex.hIconSm          = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_BOOK));

    return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;

   hInst = hInstance; // Store instance handle in our global variable

   // prepare for XP style controls
   InitCommonControls();

   // update for dpi
   UpdateRectForDpi(&_header->placement.rcNormalPosition);
   UpdateFontForDpi(&_header->font);
#if ENABLE_TAG
   for (int i = 0; i < MAX_TAG_COUNT; i++)
   {
       UpdateFontForDpi(&_header->tags[i].font);
   }
#endif

   // fixed rect exception bug.
#if 0
    int sw,sh;
    sw = GetCxScreenForDpi();
    sh = GetCyScreenForDpi();
    if (_header->rect.left < 0 && _header->rect.right - _header->rect.left <= sw)
    {
        _header->rect.right -= _header->rect.left;
        _header->rect.left = 0;
    }
    if (_header->rect.right > sw && _header->rect.right - _header->rect.left <= sw)
    {
        _header->rect.left -= _header->rect.right - sw;
        _header->rect.right = sw;
    }
    if (_header->rect.top < 0 && _header->rect.bottom - _header->rect.top <= sh)
    {
        _header->rect.bottom -= _header->rect.top;
        _header->rect.top = 0;
    }
    if (_header->rect.bottom > sh && _header->rect.bottom - _header->rect.top <= sh)
    {
        _header->rect.top -= _header->rect.bottom - sh;
        _header->rect.bottom = sh;
    }
    if (_header->rect.left < 0 || _header->rect.top < 0
        || _header->rect.right > sw || _header->rect.bottom > sh)
    {
        // default rect
        _header->rect.left = (GetCxScreenForDpi() - DEFAULT_APP_WIDTH) / 2;
        _header->rect.top = (GetCyScreenForDpi() - DEFAULT_APP_HEIGHT) / 2;
        _header->rect.right = _header->rect.left + DEFAULT_APP_WIDTH;
        _header->rect.bottom = _header->rect.top + DEFAULT_APP_HEIGHT;
        UpdateLayoutForDpi(&_header->rect);
    }
#endif
   
   _header->style &= ~WS_MINIMIZE;
   _header->style &= ~WS_DISABLED;
   if (_header->placement.showCmd == SW_SHOWMINIMIZED || _header->placement.showCmd == SW_MINIMIZE)
   {
       _header->placement.showCmd = SW_SHOWNORMAL;
   }

   hWnd = CreateWindowEx(_header->exstyle, szWindowClass, szTitle, _header->style | WS_CLIPCHILDREN,
       _header->placement.rcNormalPosition.left, _header->placement.rcNormalPosition.top, 
       _header->placement.rcNormalPosition.right - _header->placement.rcNormalPosition.left,
       _header->placement.rcNormalPosition.bottom - _header->placement.rcNormalPosition.top,
      NULL, NULL, hInstance, NULL);

   SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
   SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);

   SetWindowPlacement(hWnd, &_header->placement);

   if (!hWnd)
   {
      return FALSE;
   }
   _hWnd = hWnd;

   if (!_WndInfo.bLayered)
   {
       SetLayeredWindowAttributes(hWnd, 0, _header->alpha < MIN_ALPHA_VALUE ? MIN_ALPHA_VALUE : _header->alpha, LWA_ALPHA);
   }

   ShowSysTray(hWnd, _header->show_systray);
   ShowInTaskbar(hWnd, !_header->hide_taskbar);

   if (_WndInfo.status != ds_normal)
   {
       OnDraw(hWnd);
   }

   //ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND    - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY    - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;
    LRESULT hit;
    POINT pt;
    RECT rc;
    int caption_h;
    int border_w;
    static RECT s_rect;

    if (message == _uFindReplaceMsg)
    {
        OnFindText(hWnd, message, wParam, lParam);
        return 0;
    }
    if (WM_TASKBAR_CREATED == message)
    {
        // Explorer may recreate taskbar buttons for visible top-level windows.
        // Reapply the user's taskbar preference before restoring any tray icon.
        ShowInTaskbar(hWnd, !_header->hide_taskbar);
        if (_header->show_systray)
        {
            // Explorer discarded the old icon, so force it to be added again.
            _nid.uFlags = 0;
            ShowSysTray(hWnd, TRUE);
        }
    }
    switch (message)
    {
    case WM_COMMAND:
        PauseAutoPage(hWnd);
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        if (wmId >= IDM_OPEN_BEGIN && wmId <= IDM_OPEN_END)
        {
            item_t* item = _Cache.get_item(wmId - IDM_OPEN_BEGIN);
            if (item) OnOpenItem(hWnd, item->id, FALSE);
            ResumeAutoPage(hWnd);
            break;
        }
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_OPEN:
            OnOpenFile(hWnd, message, wParam, lParam);
            break;
        case IDM_LIBRARY:
            OpenLibrary(hWnd, _Cache);
            break;
        case IDM_NAV_BACK:
        case IDM_NAV_FORWARD:
            NavigateHistory(hWnd, wmId == IDM_NAV_FORWARD);
            SetFocus(hWnd);
            break;
        case IDM_READER_SEARCH:
            OnSearch(hWnd, message, wParam, lParam);
            break;
        case IDM_FOCUS:
            _focusMode = !_focusMode;
            if (_focusMode) ShowWindow(_hTreeMark, SW_HIDE);
            UpdateReaderLayout(hWnd); Invalidate(hWnd, TRUE, FALSE); SetFocus(hWnd);
            break;
        case IDM_CLEAR:
            OnClearFileList(hWnd, message, wParam, lParam);
            break;
        case IDM_DISPLAY:
            OpenDisplaySetDlg();
            break;
        case IDM_CONFIG:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTING), hWnd, Setting);
            break;
        case IDM_KEYSET:
            KS_OpenDlg();
            break;
        case IDM_ADVSET:
            ADV_OpenDlg(hInst, hWnd, &(_header->chapter_rule));
            break;
#ifdef ENABLE_NETWORK
        case IDM_ONLINE:
            OpenOnlineDlg();
            break;
        case IDM_WEB:
            OpenWebDlg();
            break;
#endif
#if ENABLE_TAG
        case IDM_TAGSET:
            TS_OpenDlg();
            if (_Book && !_Book->IsLoading())
            {
                _Book->ReDraw(hWnd);
            }
            break;
#endif
        case IDM_PROXY:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_PROXY), hWnd, Proxy);
            break;
        case IDM_DEFAULT:
            OnRestoreDefault(hWnd, message, wParam, lParam);
            break;
        case IDM_VIEW:
            ToggleToc(hWnd);
            break;
        case IDM_MARK:
            if (IsWindowVisible(_hTreeMark)) ShowWindow(_hTreeMark, SW_HIDE);
            else if (_Book && !_Book->IsLoading() && _item)
            {
                if (_item->mark_size <= 0)
                {
                    MessageBox(hWnd, _T("当前没有书签。按 Ctrl+M 添加当前位置。"), _T("书签"), MB_OK | MB_ICONINFORMATION);
                    break;
                }
                GetReadingRect(hWnd, &rc);
                SetWindowPos(_hTreeMark, NULL, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, SWP_SHOWWINDOW);
                SetFocus(_hTreeMark);
            }
            UpdateReaderLayout(hWnd); Invalidate(hWnd, TRUE, FALSE);
            break;
        default:
            ResumeAutoPage(hWnd);
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        ResumeAutoPage(hWnd);
        break;
    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case TVN_GETINFOTIP:
            {
                auto tip = (NMTVGETINFOTIP*)lParam;
                const auto& labels = tip->hdr.hwndFrom == _hTreeView ? _chapterTitles : _markLabels;
                if ((tip->hdr.hwndFrom == _hTreeView || tip->hdr.hwndFrom == _hTreeMark) && tip->lParam >= 0 && (size_t)tip->lParam < labels.size())
                    lstrcpyn(tip->pszText, labels[(size_t)tip->lParam].c_str(), tip->cchTextMax);
            }
            break;
        case TVN_SELCHANGED:
            if (((LPNMHDR)lParam)->hwndFrom == _hTreeView && !_syncingToc
                && ((NMTREEVIEW*)lParam)->action == TVC_BYKEYBOARD)
                PostMessage(hWnd, WM_TOC_ACTIVATE, 0, 0);
            break;
        case NM_CLICK:
            if (((LPNMHDR)lParam)->hwndFrom == _hTreeView && !_syncingToc)
            {
                TVHITTESTINFO hit = {}; GetCursorPos(&hit.pt); ScreenToClient(_hTreeView, &hit.pt);
                if (TreeView_HitTest(_hTreeView, &hit) && (hit.flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)))
                    PostMessage(hWnd, WM_TOC_ACTIVATE, 0, 0);
            }
            break;
        case NM_RCLICK:
            if (((LPNMHDR)lParam)->hwndFrom == _hTreeMark)
            {
                HTREEITEM hItem = TreeView_GetNextItem(((LPNMHDR)lParam)->hwndFrom, 0, TVGN_DROPHILITE);
                if(hItem)
                    TreeView_SelectItem(((LPNMHDR)lParam)->hwndFrom, hItem);
            }
            break;
        case NM_DBLCLK:
            if (((LPNMHDR)lParam)->hwndFrom == _hTreeMark)
            {
                if (IsWindowVisible(_hTreeMark))
                {
                    HTREEITEM Selected;
                    TVITEM item;
                    Selected = TreeView_GetSelection(_hTreeMark);
                    if (Selected)
                    {
                        item.mask       = TVIF_PARAM;
                        item.hItem      = Selected;
                        TreeView_GetItem(_hTreeMark, &item);
                        if (_Book && !_Book->IsLoading() && _item)
                        {
                            if (item.lParam >= 0 && item.lParam < _item->mark_size)
                                NavigateBookmark(hWnd, (int)item.lParam);
                        }
                        //ShowWindow(_hTreeMark, SW_HIDE);
                        PostMessage(hWnd, WM_COMMAND, IDM_MARK, NULL);
                    }
                }
            }
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_CONTEXTMENU:
        if (wParam == (WPARAM)_hTreeMark)
        {
            HTREEITEM Selected;
            TVITEM item;
            pt.x = LOWORD(lParam); 
            pt.y = HIWORD(lParam);
            Selected = TreeView_GetSelection(_hTreeMark);
            if (Selected)
            {
                HMENU hMenu = CreatePopupMenu();
                if (hMenu)
                {
                    InsertMenu(hMenu, (UINT)-1, MF_BYPOSITION, IDM_MK_DEL, _T("Delete"));
                    SetForegroundWindow(hWnd);
                    int ret = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, _hTreeMark, NULL);
                    DestroyMenu(hMenu);
                    if (IDM_MK_DEL == ret)
                    {
                        item.mask = TVIF_PARAM;
                        item.hItem = Selected;
                        TreeView_GetItem(_hTreeMark, &item);

                        if (_Cache.del_mark(_item, (int)item.lParam))
                            OnUpdateBookMark(hWnd);
                    }
                }
            }
        }
        break;
    case WM_MENURBUTTONUP:
        wmId = GetMenuItemID((HMENU)lParam, (int)wParam);
        if (wmId >= IDM_OPEN_BEGIN && wmId <= IDM_OPEN_END)
        {
            item_t* item = _Cache.get_item(wmId - IDM_OPEN_BEGIN);
            if (item && IDYES == MessageBoxFmt_(hWnd, IDS_WARN, MB_ICONINFORMATION | MB_YESNO, IDS_DELETE_FILE_CFM, item->file_name))
                RemoveLibraryBook(item->id);
        }
        break;
    case WM_READER_UPDATE_MENU:
        OnUpdateMenu(hWnd); break;
    case WM_CREATE:
        OnCreate(hWnd);
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        if (!_WndInfo.bLayered)
        {
            int saved = SaveDC(hdc);
            ExcludeClipRect(hdc, _readingRect.left, _readingRect.top, _readingRect.right, _readingRect.bottom);
            FillRect(hdc, &ps.rcPaint, ThemeWindowBrush()); RestoreDC(hdc, saved);
        }
        EndPaint(hWnd, &ps);
        break;
    case WM_CLOSE:
        StopAutoPage(hWnd);
        if (_header->show_systray)
        {
            _tcscpy(_nid.szInfoTitle, szTitle);
            _tcscpy(_nid.szInfo, _T("I'm here!"));
            _nid.uTimeout = 1000;
            Shell_NotifyIcon(NIM_MODIFY, &_nid);
            ShowHideWindow(hWnd);
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_DESTROY:
        _free_resource(hWnd);
        _update_data(hWnd, FALSE, FALSE);
        PostQuitMessage(0);
        break;
    case WM_NCHITTEST:
        hit = DefWindowProc(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT && _WndInfo.status == ds_borderless)
        {
            border_w = GetWidthForDpi(8);
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            GetClientRectExceptStatusBar(hWnd, &rc);

            if (pt.x < rc.left + border_w)
            {
                if (pt.y < rc.top + border_w)
                    hit = HTTOPLEFT;
                else if (pt.y > rc.bottom - border_w)
                    hit = HTBOTTOMLEFT;
                else
                    hit = HTLEFT;
            }
            else if (pt.x > rc.right - border_w)
            {
                if (pt.y < rc.top + border_w)
                    hit = HTTOPRIGHT;
                else if (pt.y > rc.bottom - border_w)
                    hit = HTBOTTOMRIGHT;
                else
                    hit = HTRIGHT;
            }
            else if (pt.y < rc.top + border_w)
            {
                hit = HTTOP;
            }
            else if (pt.y > rc.bottom - border_w)
            {
                hit = HTBOTTOM;
            }
            else
            {
                GetReadingRect(hWnd, &rc);
                if (!PtInRect(&rc, pt)) return HTCLIENT;
                if (_header->page_mode == 2)
                {
                    int third = (rc.right - rc.left) / 3;
                    rc.right = rc.left + third * 2;
                    rc.left += third;
                    if (PtInRect(&rc, pt))
                        hit = HTCAPTION;
                }
                else
                {
                    caption_h = GetWidthForDpi(60);
                    rc.bottom = rc.top + (std::min)((int)(rc.bottom - rc.top) / 2, caption_h);
                    if (PtInRect(&rc, pt))
                        hit = HTCAPTION;
                }
            }
        }
        return hit;
    case WM_SIZING:
        if (EC_IsEditMode())
        {
            RECT *lprc = NULL;
            lprc = (RECT *)lParam;
            lprc->left = s_rect.left;
            lprc->top = s_rect.top;
            lprc->right = s_rect.right;
            lprc->bottom = s_rect.bottom;
            return TRUE;
        }
        break;
    case WM_SIZE:
        if (!IsIconic(hWnd))
            OnSize(hWnd, message, wParam, lParam);
        GetWindowRect(hWnd, &s_rect);
        ResetAutoPage(hWnd);
        break;
    case WM_KEYDOWN:
#if ENABLE_GLOBAL_KEY
        if (_hKeyboardHook)
            break;
#endif

        if (_Book && _Book->IsLoading())
            break;

        if (KS_KeyDownProc(hWnd, message, wParam, lParam))
            break;

        if (VK_ESCAPE == wParam)
        {
            if (_WndInfo.status == ds_fullscreen)
            {
                OnFullScreen(hWnd, message, wParam, lParam);
            }
            else
            {
                if (_header->disable_eschide == 0)
                    OnHideWin(hWnd, message, wParam, lParam);
            }
        }
        else if ('P' == wParam)
        {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000
                && GetAsyncKeyState(VK_SHIFT) & 0x8000
                && GetAsyncKeyState(18) & 0x8000) // alt
            {
                if (!_hMouseHook)
                    _hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, hInst, NULL);
                else
                {
                    UnhookWindowsHookEx(_hMouseHook);
                    _hMouseHook = NULL;
                }
            }
        }
#ifdef ENABLE_NETWORK
        else if (VK_F5 == wParam) // online book manual check
        {
            if (_Book && !_Book->IsLoading() && _Book->GetBookType() == book_online)
            {
                if (IDYES == MessageBox_(_hWnd, IDS_REFRESH_TIP, IDS_WARN, MB_ICONINFORMATION | MB_YESNO))
                {
                    chkbook_arg_t* arg = GetCheckBookArguments();
                    arg->book = (OnlineBook*)_Book;
                    arg->hWnd = hWnd;
                    arg->checked_list.insert(_Book->GetFileName());
                    if (2 != ((OnlineBook*)_Book)->ManualCheckUpdate(hWnd, OnManualCheckBookUpdateCallback, arg))
                    {
                        if (arg->book != _Book)
                            delete arg->book;
                        arg->book = NULL;
                    }
                }
            }
        }
#endif
        break;
    case WM_HOTKEY:
        KS_HotKeyProc(hWnd, message, wParam, lParam);
        break;
    case WM_LBUTTONDOWN:
        pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        if (!PtInRect(&_readingRect, pt)) break;
        SetFocus(hWnd);
        if (_tocVisible && _readingGeometry.drawer)
        { CloseToc(hWnd); break; }
        if ((wParam & MK_LBUTTON) && (wParam & MK_RBUTTON) && _header->disable_lrhide == 0)
        {
            ShowHideWindow(hWnd);
            break;
        }
        if (_header->page_mode == 1)
        {
            OnPageDown(hWnd, message, wParam, lParam);
        }
        else if (_header->page_mode == 2)
        {
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            GetReadingRect(hWnd, &rc);
            int origin = rc.left;
            int third = (rc.right - rc.left) / 3;
            rc.left = origin + third * 2;
            if (PtInRect(&rc, pt))
            {
                OnPageDown(hWnd, message, wParam, lParam);
            }
            else
            {
                rc.left = origin;
                rc.right = origin + third;
                if (PtInRect(&rc, pt))
                {
                    OnPageUp(hWnd, message, wParam, lParam);
                }
            }
        }
        break;
    case WM_RBUTTONDOWN:
        pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        if (!PtInRect(&_readingRect, pt)) break;
        SetFocus(hWnd);
        if (_tocVisible && _readingGeometry.drawer)
        { CloseToc(hWnd); break; }
        if ((wParam & MK_LBUTTON) && (wParam & MK_RBUTTON) && _header->disable_lrhide == 0)
        {
            ShowHideWindow(hWnd);
            break;
        }
        if (_header->page_mode == 1)
        {
            OnPageUp(hWnd, message, wParam, lParam);
        }
        break;
    case WM_NCLBUTTONDOWN:
        if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) && _header->disable_lrhide == 0)
        {
            ShowHideWindow(hWnd);
            break;
        }
        if (_hMouseHook)
        {
            switch (wParam)
            {
            case HTMINBUTTON:
                //PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                return FALSE;
            case HTMAXBUTTON:
                //PostMessage(hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
                return FALSE;
            case HTCLOSE:
                UnhookWindowsHookEx(_hMouseHook);
                _hMouseHook = NULL;
                break;
            default:
                break;
            }
        }
        if (IsWindowVisible(_hTreeMark))
        {
            ShowWindow(_hTreeMark, SW_HIDE);
            GetCursorPos(&pt);
            GetMenuItemRect(hWnd, GetMenu(hWnd), 2, &rc);
            if (PtInRect(&rc, pt))
            {
                break;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_NCRBUTTONDOWN:
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && _header->disable_lrhide == 0) //not work
        {
            ShowHideWindow(hWnd);
            break;
        }
        if (IsWindowVisible(_hTreeMark))
        {
            ShowWindow(_hTreeMark, SW_HIDE);
            GetCursorPos(&pt);
            GetMenuItemRect(hWnd, GetMenu(hWnd), 2, &rc);
            if (PtInRect(&rc, pt))
            {
                break;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_MOUSEHWHEEL:
    case WM_MOUSEWHEEL:
        {
            const BYTE MIN_ALPHA = 0x01;
            const BYTE MAX_ALPHA = 0xff;
            const BYTE UNIT_STEP = 0x05;
            if (_routingWheel) return 0;
            POINT wheel = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HWND trees[] = { _hTreeView, _hTreeMark };
            for (int i = 0; i < 2; ++i)
            {
                RECT bounds; GetWindowRect(trees[i], &bounds);
                if (IsWindowVisible(trees[i]) && PtInRect(&bounds, wheel))
                {
                    _routingWheel = TRUE;
                    SendMessage(trees[i], message, wParam, lParam);
                    _routingWheel = FALSE;
                    return 0;
                }
            }
            ScreenToClient(hWnd, &wheel);
            if (!PtInRect(&_readingRect, wheel)) return 0;
            if (message == WM_MOUSEHWHEEL) return 0;
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
            {
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                {
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                    {
                        _header->alpha = MAX_ALPHA;
                    }
                    else
                    {
                        if (_header->alpha < MAX_ALPHA - UNIT_STEP)
                            _header->alpha += UNIT_STEP;
                        else
                            _header->alpha = MAX_ALPHA;
                    }
                    if (_WndInfo.bLayered)
                        OnDraw(hWnd);
                    else
                        SetLayeredWindowAttributes(hWnd, 0, _header->alpha < MIN_ALPHA_VALUE ? MIN_ALPHA_VALUE : _header->alpha, LWA_ALPHA);
                }
                else if (GetAsyncKeyState(18) & 0x8000) // alt
                {
                    if (_textAlpha < MAX_ALPHA - UNIT_STEP)
                        _textAlpha += UNIT_STEP;
                    else
                        _textAlpha = MAX_ALPHA;
                    if (_WndInfo.bLayered)
                        OnDraw(hWnd);
                }
                else
                {
                    OnLineUp(hWnd, message, wParam, lParam);
                }
            }
            else
            {
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                {
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                    {
                        _header->alpha = MIN_ALPHA;
                    }
                    else
                    {
                        if (_header->alpha > MIN_ALPHA + UNIT_STEP)
                            _header->alpha -= UNIT_STEP;
                        else
                            _header->alpha = MIN_ALPHA;
                    }
                    if (_WndInfo.bLayered)
                        OnDraw(hWnd);
                    else
                        SetLayeredWindowAttributes(hWnd, 0, _header->alpha < MIN_ALPHA_VALUE ? MIN_ALPHA_VALUE : _header->alpha, LWA_ALPHA);
                }
                else if (GetAsyncKeyState(18) & 0x8000) // alt
                {
                    if (_textAlpha > 0x1f + UNIT_STEP)
                        _textAlpha -= UNIT_STEP;
                    else
                        _textAlpha = 0x1f;
                    if (_WndInfo.bLayered)
                        OnDraw(hWnd);
                }
                else
                {
                    OnLineDown(hWnd, message, wParam, lParam);
                }
            }
        }
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_DROPFILES:
        OnDropFiles(hWnd, message, wParam, lParam);
        break;
    case WM_TIMER:
        switch (wParam)
        {
        case IDT_TIMER_PAGE:
            if ((_header->autopage_mode & 0x0f) == apm_page)
                OnPageDown(hWnd, message, wParam, lParam);
            else
                OnLineDown(hWnd, message, wParam, lParam);
            if (_Book && _Book->IsLastPage())
                StopAutoPage(hWnd);
            else
                ResetAutoPage(hWnd);
            break;
#ifdef ENABLE_NETWORK
        case IDT_TIMER_UPGRADE:
            _Upgrade.Check(UpgradeCallback, hWnd);
            break;
        case IDT_TIMER_CHECKBOOK:
            OnCheckBookUpdate(hWnd);
            break;
#endif
        case IDT_TIMER_LOADING:
            {
                GUID Guid;
                KillTimer(hWnd, IDT_TIMER_LOADING);
                Guid = Gdiplus::FrameDimensionTime;
                _loading->image->SelectActiveFrame(&Guid, _loading->frameIndex);
                SetTimer(hWnd, IDT_TIMER_LOADING, ((UINT*)(_loading->item[0].value))[_loading->frameIndex] * 10, NULL);
                _loading->frameIndex = (++ _loading->frameIndex) % _loading->frameCount;
                Invalidate(hWnd, TRUE, FALSE);
            }
            break;
        default:
            break;
        }
        break;
    case WM_TOC_ACTIVATE:
        if (_tocVisible && !_syncingToc && _Book && !_Book->IsLoading())
        {
            TVITEM item = {}; item.hItem = TreeView_GetSelection(_hTreeView); item.mask = TVIF_PARAM;
            if (item.hItem && TreeView_GetItem(_hTreeView, &item) && BeginReadingJump())
            { _Book->JumpChapter(hWnd, (int)item.lParam); _Book->ResetLayout(); }
        }
        break;
    case WM_UPDATE_CHAPTERS:
        OnUpdateChapters(hWnd);
        OnUpdateBookMark(hWnd);
        break;
#ifdef ENABLE_NETWORK
    case WM_NEW_VERSION:
        DialogBox(hInst, MAKEINTRESOURCE(IDD_UPGRADE), hWnd, UpgradeProc);
        break;
#endif
    case WM_OPEN_BOOK:
        if (ReaderOpenResultIsCurrent(_Book ? _Book->GetOpenToken() : 0, (UINT)lParam))
            OnOpenBookResult(hWnd, wParam == 1);
        break;
    case WM_COPYDATA:
        OnCopyData(hWnd, message, wParam, lParam);
        break;
    case WM_BOOK_EVENT:
        {
            wchar_t* previousText = _Book ? _Book->GetText() : NULL;
            int previousLength = _Book ? _Book->GetTextLength() : 0;
            int previousChapter = _Book ? _Book->GetCurChapterIndex() : -1;
            book_event_data_t* be = (book_event_data_t*)lParam;
            if (_Book && (!be || _Book == be->_this))
                _Book->OnBookEvent(hWnd, message, wParam, lParam);
#ifdef ENABLE_NETWORK
            else
            {
                chkbook_arg_t* arg = GetCheckBookArguments();
                be = (book_event_data_t*)lParam;
                if (arg && arg->book && be && arg->book == be->_this)
                    arg->book->OnBookEvent(hWnd, message, wParam, lParam);
            }
#endif
            if (_Book && (_Book->GetText() != previousText || _Book->GetTextLength() != previousLength
                || ((_Book->GetBookType() == book_online || _Book->GetBookType() == book_web) && _Book->GetCurChapterIndex() != previousChapter)))
                InvalidateReadingText();
        }
        break;
    case WM_SAVE_CACHE:
        OnSave(hWnd);
        break;
    case WM_SYSTRAY:
        switch(lParam)
        {
        case WM_LBUTTONUP:
            if (!IsWindowVisible(hWnd))
                ShowWindow(hWnd, SW_SHOW);
            if (IsIconic(hWnd))
                ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            break;
        case WM_RBUTTONDOWN:
        case WM_CONTEXTMENU:
            {
                int ret;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if(hMenu)
                {
                    InsertMenu(hMenu, (UINT)-1, MF_BYPOSITION, IDM_ST_OPEN, _T("Open"));
                    InsertMenu(hMenu, (UINT)-1, MF_BYPOSITION, IDM_ST_EXIT, _T("Exit"));
                    SetForegroundWindow(hWnd);
                    ret = TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    if (IDM_ST_OPEN == ret)
                    {
                        if (!IsWindowVisible(hWnd))
                            ShowWindow(hWnd, SW_SHOW);
                        if (IsIconic(hWnd))
                            ShowWindow(hWnd, SW_RESTORE);
                        SetForegroundWindow(hWnd);
                    }
                    else if (IDM_ST_EXIT == ret)
                    {
                        DestroyWindow(hWnd);
                    }
                }
            }
            break;
        }
        break;
    case WM_QUERYENDSESSION:
        //Exit(); // save data when poweroff ?
        return TRUE;
    case WM_ENDSESSION:
        _free_resource(hWnd);
        _update_data(hWnd, FALSE, FALSE);
        Exit();
        return 0;
        break;
    case 0x02E0: //WM_DPICHANGED
        {
            CancelSplitter(hWnd);
            NONCLIENTMETRICS theMetrics;
            theMetrics.cbSize = sizeof(NONCLIENTMETRICS);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(NONCLIENTMETRICS), (PVOID) &theMetrics,0);
            HFONT hFont = CreateFontIndirect(&(theMetrics.lfMenuFont));
            SendMessage(_hTreeView, WM_SETFONT, (WPARAM)hFont, NULL);
            SendMessage(_hTreeView, TVM_SETITEMHEIGHT, theMetrics.iMenuHeight, NULL);
            SendMessage(_hTreeMark, WM_SETFONT, (WPARAM)hFont, NULL);
            SendMessage(_hTreeMark, TVM_SETITEMHEIGHT, theMetrics.iMenuHeight, NULL);
            DpiChanged(hWnd, &_header->font, &_header->placement.rcNormalPosition, wParam, (RECT*)lParam);
            DpiChanged(hWnd, &_header->font, &_header->fs_placement.rcNormalPosition, wParam, (RECT*)lParam);
            HFONT previousFont = _readerUIFont;
            theMetrics.lfMessageFont.lfHeight = -MulDiv(9, LOWORD(wParam), 72);
            theMetrics.lfMessageFont.lfWidth = 0;
            _readerUIFont = CreateFontIndirect(&theMetrics.lfMessageFont);
            for (int i = 0; i < 8; ++i) SendMessage(_readerButtons[i], WM_SETFONT, (WPARAM)_readerUIFont, TRUE);
            if (previousFont) DeleteObject(previousFont);
            UpdateReaderLayout(hWnd); Invalidate(hWnd, TRUE, FALSE);
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

Gdiplus::Bitmap *LoadBarCodeAlipay(void)
{
    static Gdiplus::Bitmap *image = NULL;
    IStream* pStream = NULL;
    void *buffer = NULL;
    HGLOBAL hMemory = NULL;
    static int i = 0;

    if (!image)
    {
        // decrypto
        for (; i<BARCODE_ALIPAY_LENGHT; i++)
            barcode_alipay[i] ^= CRYPTO_KEY;
        hMemory = ::GlobalAlloc(GMEM_MOVEABLE, BARCODE_ALIPAY_LENGHT);
        if (!hMemory)
        {
            return image;
        }
        buffer = ::GlobalLock(hMemory);
        if (!buffer)
        {
            ::GlobalFree(hMemory);
            return image;
        }
        CopyMemory(buffer, barcode_alipay, BARCODE_ALIPAY_LENGHT);
        if (::CreateStreamOnHGlobal(hMemory, FALSE, &pStream) == S_OK)
        {
            image = Gdiplus::Bitmap::FromStream(pStream);
            pStream->Release();
        }
        ::GlobalUnlock(hMemory);
        //::GlobalFree(hMemory);
    }

    if (!image || Gdiplus::Ok != image->GetLastStatus())
    {
        delete image;
        image = NULL;
        ::GlobalFree(hMemory);
    }

    return image;
}

Gdiplus::Bitmap* LoadBarCodeWechat(void)
{
    static Gdiplus::Bitmap* image = NULL;
    IStream* pStream = NULL;
    void* buffer = NULL;
    HGLOBAL hMemory = NULL;
    static int i = 0;

    if (!image)
    {
        for (; i<BARCODE_WECHAT_LENGHT; i++)
            barcode_wechat[i] ^= CRYPTO_KEY;
        hMemory = ::GlobalAlloc(GMEM_MOVEABLE, BARCODE_WECHAT_LENGHT);
        if (!hMemory)
        {
            return image;
        }
        buffer = ::GlobalLock(hMemory);
        if (!buffer)
        {
            ::GlobalFree(hMemory);
            return image;
        }
        CopyMemory(buffer, barcode_wechat, BARCODE_WECHAT_LENGHT);
        if (::CreateStreamOnHGlobal(hMemory, FALSE, &pStream) == S_OK)
        {
            image = Gdiplus::Bitmap::FromStream(pStream);
            pStream->Release();
        }
        ::GlobalUnlock(hMemory);
        //::GlobalFree(hMemory);
    }

    if (!image || Gdiplus::Ok != image->GetLastStatus())
    {
        delete image;
        image = NULL;
        ::GlobalFree(hMemory);
    }

    return image;
}

Gdiplus::Bitmap* LoadBarCodeMP(void)
{
    static Gdiplus::Bitmap* image = NULL;
    IStream* pStream = NULL;
    void* buffer = NULL;
    HGLOBAL hMemory = NULL;
    static int i = 0;

    if (!image)
    {
        for (; i<BARCODE_MP_LENGHT; i++)
            barcode_mp[i] ^= CRYPTO_KEY;
        hMemory = ::GlobalAlloc(GMEM_MOVEABLE, BARCODE_MP_LENGHT);
        if (!hMemory)
        {
            return image;
        }
        buffer = ::GlobalLock(hMemory);
        if (!buffer)
        {
            ::GlobalFree(hMemory);
            return image;
        }
        CopyMemory(buffer, barcode_mp, BARCODE_MP_LENGHT);
        if (::CreateStreamOnHGlobal(hMemory, FALSE, &pStream) == S_OK)
        {
            image = Gdiplus::Bitmap::FromStream(pStream);
            pStream->Release();
        }
        ::GlobalUnlock(hMemory);
        //::GlobalFree(hMemory);
    }

    if (!image || Gdiplus::Ok != image->GetLastStatus())
    {
        delete image;
        image = NULL;
        ::GlobalFree(hMemory);
    }

    return image;
}

INT_PTR CALLBACK RewardProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    Gdiplus::Bitmap *image_alipay = NULL;
    Gdiplus::Bitmap* image_wechat = NULL;
    static RECT rc_alipay;
    static RECT rc_wechat;
    static RECT client;
    double dpiscaled;

    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        image_alipay = LoadBarCodeAlipay();
        image_wechat = LoadBarCodeWechat();
        if (image_alipay && image_wechat)
        {
            // calc alipay image
            dpiscaled = GetDpiScaled();
            if (dpiscaled <= 1.0f)
            {
                ::SetRect(&rc_alipay, 0, 0, (int)(image_alipay->GetWidth()/2.0f), (int)(image_alipay->GetHeight()/2.0f));
            }
            else if (dpiscaled <= 1.25f)
            {
                ::SetRect(&rc_alipay, 0, 0, (int)(image_alipay->GetWidth()/3.0f*2.0f), (int)(image_alipay->GetHeight()/3.0f*2.0f));
            }
            else
            {
                ::SetRect(&rc_alipay, 0, 0, image_alipay->GetWidth(), image_alipay->GetHeight());
            }

            // calc wechat image
            if (image_wechat)
            {
                rc_wechat.top = rc_alipay.top;
                rc_wechat.bottom = rc_alipay.bottom;
                rc_wechat.left = rc_alipay.right + 2;
                rc_wechat.right = (rc_wechat.bottom - rc_wechat.top) * image_wechat->GetWidth() / image_wechat->GetHeight() + rc_wechat.left;
            }

            // calc client
            client.top = rc_alipay.top;
            client.bottom = rc_alipay.bottom;
            client.left = rc_alipay.left;
            client.right = rc_wechat.right;

            AdjustWindowRect(&client, (DWORD)GetWindowLongPtr(hDlg, GWL_STYLE), FALSE);
            SetWindowPos(hDlg, NULL, client.left, client.top, client.right-client.left, client.bottom-client.top, SWP_NOMOVE);
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    case WM_PAINT:
        {
            HDC hdc;
            PAINTSTRUCT ps;
            Gdiplus::Graphics *graphics = NULL;
            Gdiplus::Rect rect;
            
            hdc = BeginPaint(hDlg, &ps);
            // TODO: Add any drawing code here...
            image_alipay = LoadBarCodeAlipay();
            image_wechat = LoadBarCodeWechat();

            // draw image
            graphics = new Gdiplus::Graphics(hdc);
            if (graphics)
            {
                rect.X = 0;
                rect.Y = 0;
                rect.Width = rc_alipay.right- rc_alipay.left;
                rect.Height = rc_alipay.bottom- rc_alipay.top;
                graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics->DrawImage(image_alipay, rect, 0, 0, image_alipay->GetWidth(), image_alipay->GetHeight(), Gdiplus::UnitPixel);
                rect.X = rc_wechat.left;
                rect.Y = 0;
                rect.Width = rc_wechat.right - rc_wechat.left;
                rect.Height = rc_wechat.bottom - rc_wechat.top;
                graphics->DrawImage(image_wechat, rect, 0, 0, image_wechat->GetWidth(), image_wechat->GetHeight(), Gdiplus::UnitPixel);
                delete graphics;
            }

            EndPaint(hDlg, &ps);
            return (INT_PTR)TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        EndDialog(hDlg, LOWORD(wParam));
        return (INT_PTR)TRUE;
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK FollowProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    Gdiplus::Bitmap* image_mp = NULL;
    static RECT rc_mp;
    double dpiscaled;

    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        image_mp = LoadBarCodeMP();
        dpiscaled = GetDpiScaled();
        if (image_mp)
        {
            // calc mp image
            ::SetRect(&rc_mp, 0, 0, (int)(image_mp->GetWidth() * dpiscaled), (int)(image_mp->GetHeight() * dpiscaled));

            AdjustWindowRect(&rc_mp, (DWORD)GetWindowLongPtr(hDlg, GWL_STYLE), FALSE);
            SetWindowPos(hDlg, NULL, rc_mp.left, rc_mp.top, rc_mp.right - rc_mp.left, rc_mp.bottom - rc_mp.top, SWP_NOMOVE);
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    case WM_PAINT:
    {
        HDC hdc;
        PAINTSTRUCT ps;
        Gdiplus::Graphics* graphics = NULL;
        Gdiplus::Rect rect;

        hdc = BeginPaint(hDlg, &ps);
        // TODO: Add any drawing code here...
        image_mp = LoadBarCodeMP();

        // draw image
        graphics = new Gdiplus::Graphics(hdc);
        if (graphics)
        {
            rect.X = 0;
            rect.Y = 0;
            rect.Width = rc_mp.right - rc_mp.left;
            rect.Height = rc_mp.bottom - rc_mp.top;
            graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics->DrawImage(image_mp, rect, 0, 0, image_mp->GetWidth(), image_mp->GetHeight(), Gdiplus::UnitPixel);
            delete graphics;
        }

        EndPaint(hDlg, &ps);
        return (INT_PTR)TRUE;
    }
    break;
    case WM_LBUTTONDOWN:
        EndDialog(hDlg, LOWORD(wParam));
        return (INT_PTR)TRUE;
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    POINT pt;
    HWND hWnd;
    RECT rc;
    TCHAR version[256];
    TCHAR info[256];

    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        GetApplicationVersion(version);
        _sntprintf(info, 256, _T("Reader, Version %s"), version);
        SetDlgItemText(hDlg, IDC_STATIC_VERSION, info);
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_BUTTON_REWARD)
        {
            if (LoadBarCodeAlipay() && LoadBarCodeWechat())
            {
                DialogBox(hInst, MAKEINTRESOURCE(IDD_REWARD), hDlg, RewardProc);
            }
        }
        else if (LOWORD(wParam) == IDC_BUTTON_FOLLOW)
        {
            if (LoadBarCodeMP())
            {
                DialogBox(hInst, MAKEINTRESOURCE(IDD_REWARD), hDlg, FollowProc);
            }
        }
        break;
    case WM_LBUTTONDOWN:
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        hWnd = GetDlgItem(hDlg, IDC_STATIC_LINK);
        ClientToScreen(hDlg, &pt);
        GetWindowRect(hWnd, &rc);
        if (PtInRect(&rc, pt))
        {
#ifdef ENABLE_NETWORK
            ShellExecute(NULL, _T("open"), _T("https://github.com/binbyu/Reader"), NULL, NULL, SW_SHOWNORMAL);
#endif
        }
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Setting(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    BOOL bResult = FALSE;
    int value = 0;
    int cid;
    LRESULT res;
    TCHAR buf[256] = { 0 };
    RECT rc = { 0 };
    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        SetDlgItemInt(hDlg, IDC_EDIT_LEFTLINE, _header->left_line_count, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_ELAPSE, _header->uElapse, TRUE);
        if (_header->page_mode == 0)
            cid = IDC_RADIO_MODE1;
        else if (_header->page_mode == 1)
            cid = IDC_RADIO_MODE2;
        else
            cid = IDC_RADIO_MODE3;
        SendMessage(GetDlgItem(hDlg, cid), BM_SETCHECK, BST_CHECKED, NULL);
        WheelSpeedInit(hDlg);
        // init window style
        SendMessage(GetDlgItem(hDlg, IDC_CHECK_TRAY), BM_SETCHECK, _header->show_systray ? BST_CHECKED : BST_UNCHECKED, NULL);
        SendMessage(GetDlgItem(hDlg, IDC_CHECK_TASKBAR), BM_SETCHECK, _header->hide_taskbar ? BST_CHECKED : BST_UNCHECKED, NULL);
        SendMessage(GetDlgItem(hDlg, IDC_CHECK_LRHIDE), BM_SETCHECK, _header->disable_lrhide ? BST_UNCHECKED : BST_CHECKED, NULL);
        SendMessage(GetDlgItem(hDlg, IDC_CHECK_ESCHIDE), BM_SETCHECK, _header->disable_eschide ? BST_UNCHECKED : BST_CHECKED, NULL);
        if ((_header->autopage_mode & 0x0f) == apm_page)
            SendMessage(GetDlgItem(hDlg, IDC_RADIO_ATPAGE), BM_SETCHECK, BST_CHECKED, NULL);
        else
            SendMessage(GetDlgItem(hDlg, IDC_RADIO_ATWHEEL), BM_SETCHECK, BST_CHECKED, NULL);
        LoadString(hInst, IDS_FIXED_TIME, buf, 256);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_APM), CB_ADDSTRING, 0, (LPARAM)buf);
        LoadString(hInst, IDS_DYNAMIC_CALC, buf, 256);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_APM), CB_ADDSTRING, 0, (LPARAM)buf);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_APM), CB_SETCURSEL, _header->autopage_mode >> 8, NULL);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            value = GetDlgItemInt(hDlg, IDC_EDIT_ELAPSE, &bResult, FALSE);
            if (!bResult || value == 0)
            {
                MessageBox_(hDlg, IDS_INVALID_AUTOPAGE, IDS_ERROR, MB_OK|MB_ICONERROR);
                return (INT_PTR)TRUE;
            }
            if (SendMessage(GetDlgItem(hDlg, IDC_COMBO_SPEED), CB_GETCURSEL, 0, NULL) != -1)
            {
                _header->wheel_speed = (int)SendMessage(GetDlgItem(hDlg, IDC_COMBO_SPEED), CB_GETCURSEL, 0, NULL);
            }
            res = SendMessage(GetDlgItem(hDlg, IDC_RADIO_MODE1), BM_GETCHECK, 0, NULL);
            if (res == BST_CHECKED)
            {
                _header->page_mode = 0;
            }
            else
            {
                res = SendMessage(GetDlgItem(hDlg, IDC_RADIO_MODE2), BM_GETCHECK, 0, NULL);
                if (res == BST_CHECKED)
                {
                    _header->page_mode = 1;
                }
                else
                {
                    _header->page_mode = 2;
                }
            }
            value = GetDlgItemInt(hDlg, IDC_EDIT_LEFTLINE, &bResult, FALSE);
            if (value != _header->left_line_count)
            {
                _header->left_line_count = value;
            }
            value = GetDlgItemInt(hDlg, IDC_EDIT_ELAPSE, &bResult, FALSE);
            if (value != (int)_header->uElapse)
            {
                _header->uElapse = value;
            }
            // show taskbar/tray or not
            res = SendMessage(GetDlgItem(hDlg, IDC_CHECK_TASKBAR), BM_GETCHECK, 0, NULL);
            if ((res == BST_CHECKED && _header->hide_taskbar == 0)
                || (res == BST_UNCHECKED && _header->hide_taskbar == 1))
            {
                _header->hide_taskbar = !_header->hide_taskbar;
                ShowInTaskbar(GetParent(hDlg), !_header->hide_taskbar);
            }
            res = SendMessage(GetDlgItem(hDlg, IDC_CHECK_TRAY), BM_GETCHECK, 0, NULL);
            if ((res == BST_CHECKED && _header->show_systray == 0)
                || (res == BST_UNCHECKED && _header->show_systray == 1))
            {
                _header->show_systray = !_header->show_systray;
                ShowSysTray(GetParent(hDlg), _header->show_systray);
            }
            res = SendMessage(GetDlgItem(hDlg, IDC_CHECK_LRHIDE), BM_GETCHECK, 0, NULL);
            _header->disable_lrhide = res == BST_CHECKED ? 0 : 1;
            res = SendMessage(GetDlgItem(hDlg, IDC_CHECK_ESCHIDE), BM_GETCHECK, 0, NULL);
            _header->disable_eschide = res == BST_CHECKED ? 0 : 1;
            res = SendMessage(GetDlgItem(hDlg, IDC_RADIO_ATPAGE), BM_GETCHECK, 0, NULL);
            _header->autopage_mode = 0;
            _header->autopage_mode |= res == BST_CHECKED ? apm_page : apm_line;
            res = SendMessage(GetDlgItem(hDlg, IDC_COMBO_APM), CB_GETCURSEL, 0, NULL);
            _header->autopage_mode |= res == 1 ? apm_count : apm_fixed;
            Save(GetParent(hDlg));
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        case IDC_CHECK_TASKBAR:
        case IDC_CHECK_TRAY:
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

#ifdef ENABLE_NETWORK
int hapi_set_proxy_ex(proxy_t *proxy)
{
    http_proxy_t p = {0};
    char addr[64];
    char user[64];
    char pass[64];

    strcpy(addr, Utf16ToUtf8(_header->proxy.addr));
    strcpy(user, Utf16ToUtf8(_header->proxy.user));
    strcpy(pass, Utf16ToUtf8(_header->proxy.pass));
    p.enable = proxy->enable;
    p.port = proxy->port;
    p.addr = addr;
    p.user = user;
    p.pass = pass;
    return hapi_set_proxy(&p);
}
#endif

INT_PTR CALLBACK Proxy(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT res;
    BOOL bResult = FALSE;
    int value = 0;
    TCHAR buf[256] = {0};
    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        LoadString(hInst, IDS_DISABLE_PROXY, buf, 256);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_PROXY), CB_ADDSTRING, 0, (LPARAM)buf);
        LoadString(hInst, IDS_ENABLE_PROXY, buf, 256);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_PROXY), CB_ADDSTRING, 0, (LPARAM)buf);
        SendMessage(GetDlgItem(hDlg, IDC_COMBO_PROXY), CB_SETCURSEL, _header->proxy.enable ? 1 : 0, NULL);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), _header->proxy.addr);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), _header->proxy.user);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), _header->proxy.pass);
        if (_header->proxy.port == 0)
            SetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), _T(""));
        else
            SetDlgItemInt(hDlg, IDC_EDIT_PROXY_PORT, _header->proxy.port, TRUE);
        if (_header->proxy.enable)
        {
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), TRUE);
        }
        else
        {
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), FALSE);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), buf, 64-1);
            if (buf[0])
            {
                value = GetDlgItemInt(hDlg, IDC_EDIT_PROXY_PORT, &bResult, FALSE);
                if (!bResult || value <= 0 || value >= 0xFFFF)
                {
                    MessageBox_(hDlg, IDS_INVALID_PORT, IDS_ERROR, MB_OK|MB_ICONERROR);
                    return (INT_PTR)TRUE;
                }
                _header->proxy.port = value;
            }
            else
            {
                _header->proxy.port = 0;
            }

            res = SendMessage(GetDlgItem(hDlg, IDC_COMBO_PROXY), CB_GETCURSEL, 0, NULL);
            if (res == 1)
            {
                _header->proxy.enable = TRUE;
            }
            else
            {
                _header->proxy.enable = FALSE;
            }
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), buf, 64-1);
            wcscpy(_header->proxy.addr, buf);
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), buf, 64-1);
            wcscpy(_header->proxy.user, buf);
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), buf, 64-1);
            wcscpy(_header->proxy.pass, buf);
            Save(GetParent(hDlg));
#ifdef ENABLE_NETWORK
                // update proxy to libhttps
            hapi_set_proxy_ex(&_header->proxy);
#endif
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        case IDC_COMBO_PROXY:
            res = SendMessage(GetDlgItem(hDlg, IDC_COMBO_PROXY), CB_GETCURSEL, 0, NULL);
            if (res == 0)
            {
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), FALSE);
            }
            else
            {
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_ADDR), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PORT), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_USER), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROXY_PASS), TRUE);
            }
            break;
        }
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK JumpProgress(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hWnd;
    TCHAR buf[16] = {0};
    double progress;

    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        hWnd = GetDlgItem(hDlg, IDC_EDIT_JP);
        if (_Book)
        {
            progress = _Book->GetProgress();
            _stprintf(buf, _T("%0.2f"), progress);
            SetWindowText(hWnd, buf);
        }
        SetFocus(hWnd);
        SendMessage(hWnd, EM_SETSEL, 0, -1);
        return (INT_PTR)FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            if (LOWORD(wParam) == IDOK)
            {
                if (_item)
                {
                    hWnd = GetDlgItem(hDlg, IDC_EDIT_JP);
                    GetWindowText(hWnd, buf, 15);
                    progress = _tstof(buf);
                    if (progress < 0.0 || progress > 100.0)
                    {
                        MessageBox_(hDlg, IDS_INVALID_VALUE, IDS_ERROR, MB_OK|MB_ICONERROR);
                        return (INT_PTR)TRUE;
                    }
                    if (_Book)
                    {
                        NavigatePosition(GetParent(hDlg), (int)(progress * _Book->GetTextLength() / 100));
                        Save(GetParent(hDlg));
                    }
                }
            }

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}

#ifdef ENABLE_NETWORK
INT_PTR CALLBACK UpgradeProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    json_item_data_t *vinfo;
    switch (message)
    {
    case WM_INITDIALOG:
        ThemeAttachWindow(hDlg);
        vinfo = _Upgrade.GetVersionInfo();
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPGRADE_VERSION), vinfo->version.c_str());
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPGRADE_DESC), vinfo->desc.c_str());
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDC_BUTTON_UPGRADE_INGORE:
            vinfo = _Upgrade.GetVersionInfo();
            wcscpy(_header->ingore_version, vinfo->version.c_str());
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        case IDC_BUTTON_UPGRADE_DOWN:
            vinfo = _Upgrade.GetVersionInfo();
            ShellExecute(NULL, _T("open"), vinfo->url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        break;
    default:
        break;
    }
    return (INT_PTR)FALSE;
}
#endif

BOOL LibraryCanAcceptEntries()
{
    return IsWindowEnabled(_hWnd) && (!_Book || !_Book->IsLoading()) && !EC_IsEditMode();
}

BOOL IsLibraryBookFile(const TCHAR* path)
{
    return IsSupportedBookExtension(PathFindExtension(path));
}

static void RebindReadingCache()
{
    _header = _Cache.get_header();
    _item = _Cache.get_by_id(_activeBookId);
    if (_Book)
    {
        _Book->Rebind(_item ? &_item->index : NULL, _header);
        if (!_Book->IsLoading())
        {
            _Book->SetChapterRule(&_header->chapter_rule);
            if (_item) _Book->SetFileName(_item->file_name);
        }
    }
}

void ImportLibraryEntries(const std::vector<item_t>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const item_t& entry = entries[i];
        item_t* item = _Cache.new_item(entry.file_name, entry.file_key);
        if (!item) continue;
        // A renamed original keeps its file identity and all reading data.
        if (entry.file_key[0] && _tcscmp(item->file_key, entry.file_key) == 0
            && _tcsicmp(item->file_name, entry.file_name) != 0)
        {
            int id = item->id;
            _Cache.relink_item(id, entry.file_name, entry.file_key);
            item = _Cache.get_by_id(id);
        }
        if (entry.title[0]) _tcscpy(item->title, entry.title);
        if (entry.author[0]) _tcscpy(item->author, entry.author);
    }
    RebindReadingCache();
}

void OpenLibraryBook(int id) { OnOpenItem(_hWnd, id, FALSE); }
void SaveLibrary() { if (_hWnd) { RebindReadingCache(); _update_data(_hWnd, TRUE, TRUE); OnUpdateMenu(_hWnd); } }

BOOL RemoveLibraryBook(int id) { return RemoveLibraryBooks(std::vector<int>(1, id)) == 1; }

int RemoveLibraryBooks(const std::vector<int>& ids)
{
    if (ids.empty()) return 0;
    bool close = std::find(ids.begin(), ids.end(), _activeBookId) != ids.end();
    if (_Book && _Book->IsLoading())
    {
        item_t* loading = _Cache.find_item(_Book->GetFileName());
        close = close || (loading && std::find(ids.begin(), ids.end(), loading->id) != ids.end());
    }
    if (close)
    {
        if (EC_IsEditMode()) EC_LeaveEditMode();
        StopAutoPage(_hWnd); StopLoadingImage(_hWnd);
#ifdef ENABLE_NETWORK
        chkbook_arg_t* checking = GetCheckBookArguments();
        if (checking && checking->book == _Book) checking->book = NULL;
#endif
        delete _Book; _Book = NULL;
        _item = NULL; _activeBookId = -1;
        _readingHistory.Clear(); _jumpPending = _restorePending = FALSE;
        InvalidateReadingText();
    }
    int removed = _Cache.delete_items(ids);
    RebindReadingCache();
    OnUpdateChapters(_hWnd); OnUpdateBookMark(_hWnd);
    SaveLibrary(); LibraryChanged(); UpdateTitle(_hWnd); Invalidate(_hWnd, TRUE, FALSE);
    return removed;
}

void SetLibraryBooksState(const std::vector<int>& ids, int favorite, int state)
{
    bool changed = false;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        item_t* item = _Cache.get_by_id(ids[i]);
        if (item && LibraryApplyState(*item, favorite, state)) changed = true;
    }
    if (changed) { SaveLibrary(); LibraryChanged(); }
}

BOOL RelinkLibraryBook(int id, const TCHAR* path, const TCHAR* key)
{
    DWORD attrs = GetFileAttributes(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    item_t* duplicate = _Cache.find_item(path, key);
    bool reopen = _activeBookId == id || (duplicate && duplicate->id == _activeBookId);
    if (!_Cache.relink_item(id, path, key)) return FALSE;
    if (reopen)
    {
        // The selected identity survives merging; avoid any stale pointer into a shifted row.
        _activeBookId = id;
        RebindReadingCache();
        OnOpenBook(_hWnd, _Cache.get_by_id(id)->file_name, TRUE);
    }
    else RebindReadingCache();
    SaveLibrary(); LibraryChanged();
    return TRUE;
}

static ReaderSearchToken CurrentSearchToken()
{
    ReaderSearchToken token;
    token.revision = _documentRevision;
    if (!_Book || !_item || _Book->IsLoading()) return token;
    token.book = _item->id;
    if (_Book->GetBookType() == book_online || _Book->GetBookType() == book_web) token.chapter = _Book->GetCurChapterIndex();
    return token;
}

std::wstring ReaderSearchScope()
{
    if (!_Book || !_item || _Book->IsLoading()) return L"范围：请先打开并加载书籍";
    if (_Book->GetBookType() == book_online || _Book->GetBookType() == book_web)
    {
        if (_Book->GetCurChapterIndex() < 0) return L"范围：在线当前章（目录尚未就绪）";
        TCHAR title[256] = {}; _Book->GetChapterTitle(title, 256);
        return std::wstring(L"范围：在线当前章 · ") + title;
    }
    return L"范围：本地书籍全文";
}

static bool CurrentSearchRange(int& begin, int& end)
{
    if (!_Book || !_item || _Book->IsLoading() || EC_IsEditMode() || !_Book->GetText() || _Book->GetTextLength() <= 0) return false;
    begin = 0; end = _Book->GetTextLength();
    ReaderSearchToken token = CurrentSearchToken();
    if ((_Book->GetBookType() == book_online || _Book->GetBookType() == book_web) && token.chapter < 0) return false;
    if (token.chapter >= 0)
    {
        chapters_t* chapters = _Book->GetChapters();
        int current = token.chapter;
        if (current >= (int)chapters->size() || (*chapters)[current].index < 0) return false;
        begin = (*chapters)[current].index;
        if ((*chapters)[current].size > 0)
            end = (int)(std::min)((long long)end, (long long)begin + (*chapters)[current].size);
        else for (int i = 0; i < (int)chapters->size(); ++i)
            if ((*chapters)[i].index > begin) end = (std::min)(end, (*chapters)[i].index);
    }
    return begin >= 0 && end > begin;
}

BOOL CaptureReaderSearch(reader_search_source_t* source)
{
    int begin = 0, end = 0;
    if (!source || !CurrentSearchRange(begin, end)) return FALSE;
    source->token = CurrentSearchToken(); source->scope = ReaderSearchScope(); source->base = begin;
    source->chapters.clear();
    chapters_t* chapters = _Book->GetChapters();
    if (source->token.chapter >= 0)
        source->chapters.push_back(ReaderTextChapter(0, (*chapters)[source->token.chapter].title));
    else for (size_t i = 0; i < chapters->size(); ++i)
        source->chapters.push_back(ReaderTextChapter((*chapters)[i].index, (*chapters)[i].title));
    source->text.assign(_Book->GetText() + begin, end - begin);
    return TRUE;
}

BOOL ActivateReaderSearch(const ReaderSearchToken& token, int position, int length)
{
    int begin = 0, end = 0;
    if (!(token == CurrentSearchToken()) || token.book < 0 || !CurrentSearchRange(begin, end)
        || position < begin || length <= 0 || position > end - length) return FALSE;
    NavigatePosition(_hWnd, position);
    _Book->SetSearchHighlight(position, length); _Book->ReDraw(_hWnd); Save(_hWnd);
    return TRUE;
}

void ClearReaderSearchHighlight()
{
    if (_Book) { _Book->SetSearchHighlight(-1, 0); if (!_Book->IsLoading()) _Book->ReDraw(_hWnd); }
}

void InvalidateReadingText()
{
    ++_documentRevision; InvalidateReaderSearch();
}

static std::wstring BookmarkSummary(int position)
{
    if (!_Book || position < 0 || position >= _Book->GetTextLength()) return L"正文尚未加载";
    int length = (std::min)(150, _Book->GetTextLength() - position);
    if (position + length < _Book->GetTextLength() && length > 0
        && ReaderHighSurrogate(_Book->GetText()[position + length - 1]) && ReaderLowSurrogate(_Book->GetText()[position + length])) --length;
    return ReaderMakeExcerpt(std::wstring(_Book->GetText() + position, length), 0, 0, 150).text;
}

static bookmark_detail_t DescribeBookmark(int index)
{
    bookmark_detail_t detail = _Cache.mark_detail(_item, index);
    if (!_Book || !_item) return detail;
    chapters_t* chapters = _Book->GetChapters();
    int position = detail.position;
    int chapter = detail.chapter;
    if (chapter >= 0)
    {
        if (chapter < (int)chapters->size())
        {
            if (detail.title.empty()) detail.title = (*chapters)[chapter].title;
            position = (*chapters)[chapter].index >= 0 ? (*chapters)[chapter].index + detail.position : -1;
        }
        else position = -1;
    }
    else if (_Book->GetBookType() == book_web)
    { if (detail.title.empty()) detail.title = L"旧书签（章节未知）"; position = -1; }
    else if (detail.title.empty())
    {
        std::vector<ReaderTextChapter> entries;
        for (size_t i = 0; i < chapters->size(); ++i) entries.push_back(ReaderTextChapter((*chapters)[i].index, (*chapters)[i].title));
        chapter = ReaderChapterAt(entries, position);
        detail.title = chapter >= 0 ? entries[chapter].title : L"正文";
    }
    if (detail.summary.empty()) detail.summary = BookmarkSummary(position);
    if (detail.title.empty()) detail.title = L"正文";
    return detail;
}

static ReadingAnchor CurrentReadingAnchor()
{
    if (!_item || !_Book) return ReadingAnchor();
    bool relative = _Book->GetBookType() == book_online || _Book->GetBookType() == book_web;
    int chapter = _Book->GetCurChapterIndex();
    int offset = _item->index;
    if (relative && chapter >= 0 && chapter < (int)_Book->GetChapters()->size())
        offset -= (*_Book->GetChapters())[chapter].index;
    return ReadingAnchor(_item->id, chapter, (std::max)(0, offset), relative);
}

static BOOL BeginReadingJump()
{
    if (!_item || !_Book || _Book->IsLoading() || EC_IsEditMode()) return FALSE;
    FinishReadingJump();
    _Book->SetSearchHighlight(-1, 0);
    _jumpOrigin = CurrentReadingAnchor(); _jumpPending = TRUE;
    _replaying = _restorePending = FALSE;
    return TRUE;
}

static void NavigatePosition(HWND hWnd, int position)
{
    if (!BeginReadingJump()) return;
    _Book->SetReadingPosition(position);
    _Book->ReDraw(hWnd);
}

static void NavigateHistory(HWND hWnd, BOOL forward)
{
    if (!_item || !_Book || _Book->IsLoading() || EC_IsEditMode()) return;
    FinishReadingJump();
    _replayHistory = _readingHistory;
    ReadingAnchor target;
    if (!(forward ? _replayHistory.Forward(CurrentReadingAnchor(), target)
        : _replayHistory.Back(CurrentReadingAnchor(), target))) return;
    if (target.book != _item->id) return;
    _restoreAnchor = target; _restorePending = _replaying = _jumpPending = TRUE;
    if (target.relative && target.chapter != _Book->GetCurChapterIndex())
        _Book->JumpChapter(hWnd, target.chapter);
    else
    {
        PrepareReadingJump(hWnd);
        _Book->ReDraw(hWnd);
    }
}

static void PrepareReadingJump(HWND hWnd)
{
    if (!_restorePending || !_Book || !_item || _Book->IsLoading()) return;
    int position = _restoreAnchor.offset;
    if (_restoreAnchor.relative)
    {
        int chapter = _Book->GetCurChapterIndex();
        if (chapter != _restoreAnchor.chapter || chapter < 0 || chapter >= (int)_Book->GetChapters()->size())
        {
            // Failed asynchronous chapter fetch: keep both history stacks untouched.
            _restorePending = _jumpPending = _replaying = FALSE;
            return;
        }
        position += (*_Book->GetChapters())[chapter].index;
    }
    _Book->SetReadingPosition(position);
    _restorePending = FALSE;
}

static void FinishReadingJump()
{
    if (!_jumpPending || !_Book || !_item || _Book->IsLoading() || _restorePending) return;
    ReadingAnchor current = CurrentReadingAnchor();
    if (_replaying) _readingHistory = _replayHistory;
    else _readingHistory.Record(_jumpOrigin, current);
    _jumpPending = _replaying = FALSE;
    EnableWindow(_readerButtons[5], _readingHistory.CanBack());
    EnableWindow(_readerButtons[6], _readingHistory.CanForward());
}

static void NavigateBookmark(HWND hWnd, int index)
{
    if (!_item || index < 0 || index >= _item->mark_size || !_Book || _Book->IsLoading()) return;
    bookmark_detail_t detail = _Cache.mark_detail(_item, index);
    if (detail.chapter < 0)
    {
        if (_Book->GetBookType() == book_web && MessageBox(hWnd, _T("旧书签没有章节信息。是否在当前章节按原位置定位？"), _T("旧书签"), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        NavigatePosition(hWnd, _item->mark[index]); return;
    }
    if (detail.chapter >= (int)_Book->GetChapters()->size() || !BeginReadingJump()) return;
    _restoreAnchor = ReadingAnchor(_item->id, detail.chapter, detail.position, true);
    _restorePending = TRUE;
    if (detail.chapter != _Book->GetCurChapterIndex()) _Book->JumpChapter(hWnd, detail.chapter);
    else { PrepareReadingJump(hWnd); _Book->ReDraw(hWnd); }
}

BOOL GetReadingRect(HWND hWnd, RECT* rect)
{
    *rect = _readingRect;
    return TRUE;
}

static void UpdateReaderLayout(HWND hWnd)
{
    if (!_header || !IsWindow(_hTreeView)) return;
    RECT client; GetClientRectExceptStatusBar(hWnd, &client);
    bool chrome = !_focusMode, toc = _tocVisible && !_focusMode;
    UINT dpi = GetWindowDpiCompat(hWnd);
    ReadingGeometry geometry = ComputeReadingGeometry(client.right, client.bottom, (int)dpi, chrome, toc, _splitterDrag.Active() ? _sidebarPreviewWidth : _header->sidebar_width);
    RECT reading = { geometry.readingLeft, geometry.readingTop, geometry.readingRight, geometry.readingBottom };
    bool changed = !EqualRect(&_readingRect, &reading) || _readingGeometry.sidebarWidth != geometry.sidebarWidth || _readingGeometry.splitterWidth != geometry.splitterWidth;
    if (changed && _Book) _Book->ResetLayout();
    _readingRect = reading; _readingGeometry = geometry;
    int rowHeight = MulDiv(28, dpi, 96), buttonWidth = client.right / geometry.columns;
    for (int i = 0; i < 8; ++i)
    {
        if (chrome) PositionControl(_readerButtons[i], NULL, i % geometry.columns * buttonWidth, i / geometry.columns * rowHeight, buttonWidth, rowHeight);
        VisibleControl(_readerButtons[i], chrome);
    }
    if (toc) PositionControl(_hTreeView, HWND_TOP, 0, geometry.toolbarHeight, geometry.sidebarWidth, (std::max)(1, (int)client.bottom - geometry.toolbarHeight));
    VisibleControl(_hTreeView, toc);
    if (toc && !geometry.drawer) PositionControl(_hSplitter, HWND_TOP, geometry.sidebarWidth, geometry.toolbarHeight, geometry.splitterWidth, (std::max)(1, (int)client.bottom - geometry.toolbarHeight));
    VisibleControl(_hSplitter, toc && !geometry.drawer);
    if (IsWindowVisible(_hTreeMark)) PositionControl(_hTreeMark, HWND_TOP, reading.left, reading.top, (std::max)(1, (int)(reading.right - reading.left)), (std::max)(1, (int)(reading.bottom - reading.top)));
    _WndInfo.bLayered = _WndInfo.status != ds_normal && !chrome && !toc && !IsWindowVisible(_hTreeMark);
    if (!_WndInfo.bLayered) PositionControl(_hReadingView, HWND_BOTTOM, reading.left, reading.top, (std::max)(1, (int)(reading.right - reading.left)), (std::max)(1, (int)(reading.bottom - reading.top)));
    VisibleControl(_hReadingView, !_WndInfo.bLayered);
    for (int i = 0; i < 8; ++i) EnableWindow(_readerButtons[i], !EC_IsEditMode());
    EnableWindow(_readerButtons[5], !EC_IsEditMode() && _readingHistory.CanBack());
    EnableWindow(_readerButtons[6], !EC_IsEditMode() && _readingHistory.CanForward());
    if (changed) { InvalidateRect(hWnd, NULL, FALSE); if (_hReadingView) InvalidateRect(_hReadingView, NULL, FALSE); }
}

static void SyncCurrentChapter(BOOL reveal)
{
    if (!_Book || _Book->IsLoading()) return;
    int current = _Book->GetCurChapterIndex();
    if (current < 0 || current >= (int)_chapterItems.size()) return;
    if (current == _tocCurrent && !reveal) return;
    _syncingToc = TRUE;
    ThemeSetCurrentChapter(_hTreeView, _chapterItems[current]);
    if (reveal) { TreeView_SelectItem(_hTreeView, _chapterItems[current]); TreeView_EnsureVisible(_hTreeView, _chapterItems[current]); }
    _syncingToc = FALSE; _tocCurrent = current;
}

static void CloseToc(HWND hWnd)
{
    CancelSplitter(hWnd);
    _tocVisible = FALSE;
    if (_tocFromFocus) _focusMode = TRUE;
    _tocFromFocus = FALSE;
    UpdateReaderLayout(hWnd); Invalidate(hWnd, TRUE, FALSE); SetFocus(hWnd);
}

static void ToggleToc(HWND hWnd)
{
    if (EC_IsEditMode()) return;
    if (_tocVisible && !_focusMode) { CloseToc(hWnd); return; }
    if (!_Book || _Book->IsLoading()) return;
    if (_focusMode) { _tocFromFocus = TRUE; _focusMode = FALSE; }
    _tocVisible = TRUE;
    if (_tocVisible) ShowWindow(_hTreeMark, SW_HIDE);
    UpdateReaderLayout(hWnd); Invalidate(hWnd, TRUE, FALSE);
    if (_tocVisible) { SyncCurrentChapter(TRUE); SetFocus(_hTreeView); }
    else SetFocus(hWnd);
}

static void CopyTreeLabel(HWND window, const std::wstring& text)
{
    if (!OpenClipboard(window)) return;
    SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory)
    {
        void* target = GlobalLock(memory);
        if (target)
        {
            memcpy(target, text.c_str(), bytes); GlobalUnlock(memory);
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) memory = NULL;
        }
        if (memory) GlobalFree(memory);
    }
    CloseClipboard();
}

static LRESULT CALLBACK TocProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_RETURN)
    {
        if (window == _hTreeView) PostMessage(GetParent(window), WM_TOC_ACTIVATE, 0, 0);
        else
        {
            TVITEM item = {}; item.hItem = TreeView_GetSelection(window); item.mask = TVIF_PARAM;
            if (item.hItem && TreeView_GetItem(window, &item)) NavigateBookmark(GetParent(window), (int)item.lParam);
        }
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        // Expose the complete label even when a chapter is wider than the viewport.
        TVITEM item = {}; item.hItem = TreeView_GetSelection(window); item.mask = TVIF_PARAM;
        const auto& labels = window == _hTreeView ? _chapterTitles : _markLabels;
        if (item.hItem && TreeView_GetItem(window, &item) && item.lParam >= 0 && (size_t)item.lParam < labels.size())
            CopyTreeLabel(window, labels[(size_t)item.lParam]);
        return 0;
    }
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT bounds; GetWindowRect(window, &bounds);
        if (!_routingWheel && !PtInRect(&bounds, point)) return SendMessage(GetParent(window), message, wParam, lParam);
        if (message == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT))
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (message == WM_MOUSEWHEEL) delta = -delta;
            SCROLLINFO scroll = { sizeof(scroll), SIF_POS }; GetScrollInfo(window, SB_HORZ, &scroll);
            scroll.nPos += MulDiv(delta, MulDiv(48, GetWindowDpiCompat(window), 96), WHEEL_DELTA);
            SendMessage(window, WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (std::max)(0, (std::min)(65535, scroll.nPos))), 0);
            return 0;
        }
    }
    return CallWindowProc(window == _hTreeView ? _tocProc : _markProc, window, message, wParam, lParam);
}

static BOOL ReaderShortcut(MSG* message)
{
    if (message->hwnd != _hWnd && !IsChild(_hWnd, message->hwnd)) return FALSE;
    if (EC_IsEditMode() || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) return FALSE;
    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    int command = 0;
    if (control && message->wParam == 'L') command = IDM_LIBRARY;
    else if (control && message->wParam == 'T') command = IDM_VIEW;
    else if (alt && message->wParam == VK_LEFT) command = IDM_NAV_BACK;
    else if (alt && message->wParam == VK_RIGHT) command = IDM_NAV_FORWARD;
    else if (message->wParam == VK_F8) command = IDM_FOCUS;
    else if (message->wParam == VK_ESCAPE && _splitterDrag.Active()) { CancelSplitter(_hWnd); return TRUE; }
    else if (message->wParam == VK_ESCAPE && (IsWindowVisible(_hTreeView) || IsWindowVisible(_hTreeMark)))
    {
        if (IsWindowVisible(_hTreeMark)) ShowWindow(_hTreeMark, SW_HIDE);
        else { CloseToc(_hWnd); return TRUE; }
        UpdateReaderLayout(_hWnd); Invalidate(_hWnd, TRUE, FALSE); SetFocus(_hWnd);
        return TRUE;
    }
    if (message->wParam == VK_TAB && !_focusMode)
    { HWND next = GetNextDlgTabItem(_hWnd, GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0); if (next) SetFocus(next); return TRUE; }
    if (message->wParam == VK_F3) { AdvanceReaderSearch((GetKeyState(VK_SHIFT) & 0x8000) != 0); return TRUE; }
    if (!command) return FALSE;
    SendMessage(_hWnd, WM_COMMAND, command, 0);
    return TRUE;
}

LRESULT OnCreate(HWND hWnd)
{
    LPWSTR* argv;
    int argc;
    BOOL flag = TRUE;
    
    _WndInfo.bLayered = (_header->style & WS_CAPTION) == 0;
    _WndInfo.bLayered_bak = _WndInfo.bLayered;
    _WndInfo.bTopMost = (_header->exstyle & WS_EX_TOPMOST) == WS_EX_TOPMOST;
    _WndInfo.status = (_header->style & WS_MINIMIZEBOX) == 0 ? ds_fullscreen : (((_header->style & WS_CAPTION) == 0) ? ds_borderless : ds_normal);

    _WndInfo.hMenu = GetMenu(hWnd);
    // create status bar
    _WndInfo.hStatusBar = CreateStatusWindow(WS_CHILD | WS_VISIBLE, _T("Please open a text."), hWnd, IDC_STATUSBAR);
    // register find text dialog
    _uFindReplaceMsg = RegisterWindowMessage(FINDMSGSTRING);
    WNDCLASS reading = {}; reading.lpfnWndProc = ReadingProc; reading.hInstance = hInst;
    reading.lpszClassName = _T("ReaderContentView"); reading.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&reading);
    _hReadingView = CreateWindow(reading.lpszClassName, _T("正文"), WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 1, 1, hWnd, NULL, hInst, NULL);
    _hSplitter = CreateWindow(_T("STATIC"), _T("调整目录宽度（左右键调整，Home 复位）"), WS_CHILD | WS_TABSTOP | SS_NOTIFY, 0, 0, 1, 1, hWnd, (HMENU)46006, hInst, NULL);
    _splitterProc = (WNDPROC)SetWindowLongPtr(_hSplitter, GWLP_WNDPROC, (LONG_PTR)SplitterProc);
    // create chapters tree view
    _hTreeView = CreateWindow(WC_TREEVIEW, _T("Tree View"), 
        /*WS_VISIBLE | */WS_CHILD | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL | TVS_HASLINES | TVS_INFOTIP | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 200, 300, hWnd, NULL, hInst, NULL);
    _hTreeMark = CreateWindow(WC_TREEVIEW, _T("Tree Mark"), 
        /*WS_VISIBLE | */WS_CHILD | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL | TVS_HASLINES | TVS_INFOTIP | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 200, 300, hWnd, NULL, hInst, NULL);
    //TreeView_SetBkColor(_hTreeView, GetSysColor(COLOR_MENU));
    //TreeView_SetBkColor(_hTreeMark, GetSysColor(COLOR_MENU));
    SetTreeviewFont();
    _tocProc = (WNDPROC)SetWindowLongPtr(_hTreeView, GWLP_WNDPROC, (LONG_PTR)TocProc);
    _markProc = (WNDPROC)SetWindowLongPtr(_hTreeMark, GWLP_WNDPROC, (LONG_PTR)TocProc);
    _focusMode = _WndInfo.status != ds_normal;
    NONCLIENTMETRICS metrics = {}; metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    metrics.lfMessageFont.lfHeight = -MulDiv(9, GetWindowDpiCompat(hWnd), 72);
    metrics.lfMessageFont.lfWidth = 0;
    _readerUIFont = CreateFontIndirect(&metrics.lfMessageFont);
    const TCHAR* labels[] = { _T("书库"), _T("目录"), _T("查找"), _T("书签"), _T("显示"), _T("返回"), _T("前进"), _T("专注") };
    const int commands[] = { IDM_LIBRARY, IDM_VIEW, IDM_READER_SEARCH, IDM_MARK, IDM_DISPLAY, IDM_NAV_BACK, IDM_NAV_FORWARD, IDM_FOCUS };
    for (int i = 0; i < 8; ++i)
    {
        _readerButtons[i] = CreateWindow(_T("BUTTON"), labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)commands[i], hInst, NULL);
        SendMessage(_readerButtons[i], WM_SETFONT, (WPARAM)_readerUIFont, TRUE);
    }
    UpdateReaderLayout(hWnd);


    RemoveMenus(hWnd, FALSE);
    OnUpdateMenu(hWnd);

    // open file
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1)
    {
        // open input argument file name
        TCHAR fullname[MAX_PATH];
        GetFullPathName(argv[1], MAX_PATH, fullname, NULL);
        // check if file vaild
        if (IsVaildFile(NULL, fullname, NULL))
        {
            OnOpenBook(hWnd, fullname, FALSE);
            flag = FALSE;
        }
    }
    if (flag)
    {
        // open the last file
        if (_header->item_count > 0)
        {
            item_t* item = _Cache.get_by_id(_header->item_id);
            if (!item) item = _Cache.get_item(0);
            if (item && PathFileExists(item->file_name)) OnOpenBook(hWnd, item->file_name, FALSE);
            else PostMessage(hWnd, WM_COMMAND, IDM_LIBRARY, 0);
        }
    }

    // check upgrade
#ifdef ENABLE_NETWORK
    CheckUpgrade(hWnd);
    StartCheckBookUpdate(hWnd);
#endif

    // init keyset
    KS_Init(hWnd, _header->keyset);

    if (_WndInfo.status > ds_normal)
    {
        ShowWindow(_WndInfo.hStatusBar, SW_HIDE);
        SetMenu(hWnd, NULL);
    }
    SetProp(hWnd, L"ReaderOwnBackground", (HANDLE)1);
    ThemeAttachWindow(hWnd);
    UpdateReaderLayout(hWnd);
    return 0;
}

LRESULT OnUpdateMenu(HWND hWnd)
{
    if (GetWindowThreadProcessId(hWnd, NULL) != GetCurrentThreadId())
    { PostMessage(hWnd, WM_READER_UPDATE_MENU, 0, 0); return 0; }
    ThemeReleaseMenus(hWnd);
    static Gdiplus::Bitmap* s_bitmap = NULL;
    static HGLOBAL s_hMemory = NULL;
    static HBITMAP hBitmap = NULL;
    TCHAR buf[MAX_LOADSTRING];
    int menu_begin_id = IDM_OPEN_BEGIN;
    HMENU hMenuBar = GetMenu(hWnd);
    HMENU hFile = GetSubMenu(hMenuBar, 0);
    MENUITEMINFO mi = { 0 };

    if (!hMenuBar)
    {
        _menuInvalid = TRUE;
        return 0;
    }

    if (hFile)
    {
        DeleteMenu(hMenuBar, 0, MF_BYPOSITION);
        hFile = NULL;
    }
    if (hBitmap)
    {
        DeleteObject(hBitmap);
        hBitmap = NULL;
    }
    if (!s_bitmap)
    {
        LoadResourceImage(MAKEINTRESOURCE(IDB_PNG_NEW), _T("PNG"), &s_bitmap, &s_hMemory);
    }
    if (s_bitmap)
    {
        s_bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);
    }
    hFile = CreateMenu();
    LoadString(hInst, IDS_MENU_OPEN, buf, MAX_LOADSTRING);
    AppendMenu(hFile, MF_STRING, IDM_LIBRARY, _T("本地书库\tCtrl+L"));
    AppendMenu(hFile, MF_STRING, IDM_OPEN, buf);
    AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    for (int i=0; i<_header->item_count && i<40; i++)
    {
        item_t* item = _Cache.get_item(i);
        AppendMenu(hFile, MF_STRING, (UINT_PTR)menu_begin_id, item->file_name);
#ifdef ENABLE_NETWORK
        if (0 == _tcsicmp(PathFindExtension(item->file_name), _T(".ol")) && item->is_new)
        {
            SetMenuItemBitmaps(hFile, menu_begin_id, MF_BITMAP, hBitmap, hBitmap);
        }
#endif
        menu_begin_id++;
    }
    if (_header->item_count > 0)
        AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    LoadString(hInst, IDS_MENU_CLEAR, buf, MAX_LOADSTRING);
    AppendMenu(hFile, MF_STRING, IDM_CLEAR, buf);
    AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    LoadString(hInst, IDS_MENU_EXIT, buf, MAX_LOADSTRING);
    AppendMenu(hFile, MF_STRING, IDM_EXIT, buf);
    LoadString(hInst, IDS_MENU_FILE, buf, MAX_LOADSTRING);
    InsertMenu(hMenuBar, 0, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hFile, buf);
    ThemeUpdateMenu(hWnd);

    return 0;
}

LRESULT OnOpenItem(HWND hWnd, int item_id, BOOL forced)
{
    item_t* item = _Cache.get_by_id(item_id);
    if (!item) return 0;
    if (EC_IsEditMode()) EC_LeaveEditMode();
    if (PathFileExists(item->file_name)) OnOpenBook(hWnd, item->file_name, forced);
    else
    {
        OpenLibrary(hWnd, _Cache);
        MessageBox(hWnd, _T("原文件已失联，阅读进度和书签仍保留。请在书库中选中此书并重新关联。"), _T("文件失联"), MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}

LRESULT OnClearFileList(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_header->item_count <= 0)
    {
        return 0;
    }

    if (IDYES != MessageBox_(hWnd, IDS_CLEAR_ALL_FILES, IDS_WARN, MB_ICONINFORMATION | MB_YESNO))
    {
        return 0;
    }

    if (_Book)
    {
        delete _Book;
        _Book = NULL;
    }
    CloseLibrary();
    _Cache.delete_all_item();
    InvalidateReadingText();
    _item = NULL; _activeBookId = -1; _readingHistory.Clear();
    _jumpPending = _restorePending = FALSE;
    LibraryChanged();
    OnUpdateMenu(hWnd);
    PostMessage(hWnd, WM_UPDATE_CHAPTERS, 0, NULL);
    Invalidate(hWnd, TRUE, FALSE);
    SetWindowText(hWnd, szTitle);
    StopLoadingImage(hWnd);
    StopAutoPage(hWnd);
    Save(hWnd);
    return 0;
}

LRESULT OnRestoreDefault(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int lastrule = _header->chapter_rule.rule;
    int lastmenufont = _header->meun_font_follow;

    if (IDNO == MessageBox_(hWnd, IDS_RESTORE_DEFAULT, IDS_WARN, MB_ICONINFORMATION|MB_YESNO))
    {
        return 0;
    }

    KS_UnRegisterAllHotKey(hWnd);
    CancelSplitter(hWnd);
    _Cache.default_header();
    UpdateRectForDpi(&_header->placement.rcNormalPosition);
    UpdateRectForDpi(&_header->fs_placement.rcNormalPosition);
    UpdateFontForDpi(&_header->font);
    ApplyReaderTheme();
    UpdateReaderLayout(hWnd);
#if 0 // needn't
#if ENABLE_TAG
    for (int i = 0; i < MAX_TAG_COUNT; i++)
    {
        UpdateFontForDpi(hWnd, &_header->tags[i].font);
    }
#endif
#endif
    SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
    SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);

    KS_RegisterAllHotKey(hWnd);
    SetGlobalKey(hWnd);
    ShowSysTray(hWnd, _header->show_systray);
    ShowInTaskbar(hWnd, !_header->hide_taskbar);

    SetWindowPlacement(hWnd, &_header->placement);

    if (!_WndInfo.bLayered)
    {
        SetLayeredWindowAttributes(hWnd, 0, _header->alpha < MIN_ALPHA_VALUE ? MIN_ALPHA_VALUE : _header->alpha, LWA_ALPHA);
    }

    if (lastrule != _header->chapter_rule.rule)
    {
        // reload book
        OnOpenItem(hWnd, _activeBookId, TRUE);
    }
    else
    {
        if (_Book)
            _Book->ReDraw(hWnd);
    }
    if (lastmenufont != _header->meun_font_follow)
        SetTreeviewFont();

    Save(hWnd);
    return 0;
}

// Ordinary reading is painted in a non-overlapping child, in child-local coordinates.
// The BeginPaint clip restricts the final copy; chrome and siblings never share this DC.
LRESULT OnPaint(HWND hWnd, HDC hdc)
{
    RECT page; GetClientRect(_hReadingView, &page);
    if (!_readingSurface.Ensure(hdc, page.right, page.bottom)) return 0;
    HDC buffer = _readingSurface.DC();
    ReaderColors colors = ThemeReadingColors(_header);
    HBRUSH brush = CreateSolidBrush(colors.background); FillRect(buffer, &page, brush); DeleteObject(brush);
    Gdiplus::Bitmap* image = LoadBGImage(page.right, page.bottom);
    if (image) { Gdiplus::Graphics graphics(buffer); graphics.DrawImage(image, 0, 0, page.right, page.bottom); }
    if (_Book && !_Book->IsLoading())
    {
        PrepareReadingJump(hWnd);
        _Book->DrawPage(hWnd, buffer, &page, FALSE);
        FinishReadingJump();
    }
    if (!_Book && !(_loading && _loading->enable))
    {
        RECT empty = page; InflateRect(&empty, -12, -12);
        HGDIOBJ previous = SelectObject(buffer, _readerUIFont);
        SetBkMode(buffer, TRANSPARENT); SetTextColor(buffer, colors.text);
        DrawText(buffer, _T("从“书库”添加电子书，或直接打开原文件。\nCtrl+L 书库 · Ctrl+T 目录 · F8 专注"), -1, &empty, DT_CENTER | DT_WORDBREAK);
        SelectObject(buffer, previous);
    }
    DrawLoading(buffer, page);
    BitBlt(hdc, 0, 0, page.right, page.bottom, buffer, 0, 0, SRCCOPY);
    UpdateProgess(); UpdateTitle(hWnd); SyncCurrentChapter();
    return 0;
}

VOID OnDraw(HWND hWnd)
{
    // Pixel transparency requires a whole-frame UpdateLayeredWindow submission.
    // Reuse the DIB/DC and Page's text DIB; no child is recreated for a reading jump.
    RECT page; GetClientRectExceptStatusBar(hWnd, &page);
    int width = page.right, height = page.bottom;
    HDC screen = GetDC(NULL);
    if (!_frameSurface.Ensure(screen, width, height)) { ReleaseDC(NULL, screen); return; }
    bool hasText = _Book && !_Book->IsLoading();
    if (hasText)
    {
        PrepareReadingJump(hWnd); _Book->DrawPage(hWnd, screen, &page, TRUE); FinishReadingJump();
        hasText = !_Book->IsBlankPage();
    }
    BYTE alpha = _header->alpha;
    if (!hasText && alpha < MIN_ALPHA_VALUE) alpha = MIN_ALPHA_VALUE;
    ReaderColors colors = ThemeReadingColors(_header);
    DWORD pixel = ((DWORD)alpha << 24) | ((GetRValue(colors.background) * alpha / 255) << 16)
        | ((GetGValue(colors.background) * alpha / 255) << 8) | (GetBValue(colors.background) * alpha / 255);
    std::fill_n((DWORD*)_frameSurface.Pixels(), (size_t)width * height, pixel);
    Gdiplus::Bitmap* image = LoadBGImage(width, height, alpha);
    {
        // A bottom-up DIB needs a negative stride when wrapped by GDI+.
        Gdiplus::Bitmap frame(width, height, -width * 4, PixelFormat32bppPARGB,
            _frameSurface.Pixels() + (size_t)(height - 1) * width * 4);
        Gdiplus::Graphics graphics(&frame);
        if (image)
        {
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            graphics.DrawImage(image, 0, 0, width, height);
        }
        if (_loading && _loading->enable && _loading->image)
        {
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            double scale = (std::min)(1.0, (std::min)((double)width / _loading->image->GetWidth(), (double)height / _loading->image->GetHeight()));
            int w = (int)(_loading->image->GetWidth() * scale), h = (int)(_loading->image->GetHeight() * scale);
            graphics.DrawImage(_loading->image, (width - w) / 2, (height - h) / 2, w, h);
        }
    }
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (hasText) AlphaBlend(_frameSurface.DC(), 0, 0, width, height, _Book->AlphaDC(), 0, 0, width, height, blend);
    RECT bounds; GetWindowRect(hWnd, &bounds);
    POINT position = { bounds.left, bounds.top }, source = { 0, 0 }; SIZE size = { width, height };
    UpdateLayeredWindow(hWnd, screen, &position, &size, _frameSurface.DC(), &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screen);
    UpdateProgess(); UpdateTitle(hWnd); SyncCurrentChapter();
}

BOOL ResetLayerd(HWND hWnd)
{
    LONG_PTR exstyle;

    if (_WndInfo.bLayered_bak != _WndInfo.bLayered)
    {
        exstyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        exstyle &= ~WS_EX_LAYERED;
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, exstyle);
        exstyle |= WS_EX_LAYERED;
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, exstyle);
        _WndInfo.bLayered_bak = _WndInfo.bLayered;
        return TRUE;
    }

    return FALSE;
}

VOID Invalidate(HWND hWnd, BOOL bOnlyClient, BOOL bErase)
{
    bool reset = ResetLayerd(hWnd) != FALSE;
    if (_WndInfo.bLayered) OnDraw(hWnd); // UpdateLayeredWindow submits a whole composed frame.
    else
    {
        if (reset) SetLayeredWindowAttributes(hWnd, 0, _header->alpha < MIN_ALPHA_VALUE ? MIN_ALPHA_VALUE : _header->alpha, LWA_ALPHA);
        if (_hReadingView) InvalidateRect(_hReadingView, NULL, FALSE);
        if (!bOnlyClient) InvalidateRect(hWnd, NULL, FALSE);
    }
}

LRESULT OnSize(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CancelSplitter(hWnd);
    SendMessage(_WndInfo.hStatusBar, message, wParam, lParam);
    UpdateReaderLayout(hWnd);
    Invalidate(hWnd, TRUE, FALSE);
    return 0;
}

LRESULT OnHideWin(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    ShowHideWindow(hWnd);
    return 0;
}

static void CorrectWindowContentRect(HWND hWnd, const RECT *target, int excludedBottom, UINT flags)
{
    RECT actualContent;
    RECT actualWindow;

    for (int i = 0; i < 3; i++)
    {
        GetClientRect(hWnd, &actualContent);
        actualContent.bottom -= excludedBottom;
        ClientToScreen(hWnd, reinterpret_cast<POINT*>(&actualContent.left));
        ClientToScreen(hWnd, reinterpret_cast<POINT*>(&actualContent.right));

        int offsetX = target->left - actualContent.left;
        int offsetY = target->top - actualContent.top;
        int offsetWidth = (target->right - target->left) - (actualContent.right - actualContent.left);
        int offsetHeight = (target->bottom - target->top) - (actualContent.bottom - actualContent.top);

        if (offsetX == 0 && offsetY == 0 && offsetWidth == 0 && offsetHeight == 0)
            break;

        GetWindowRect(hWnd, &actualWindow);
        SetWindowPos(hWnd, NULL,
            actualWindow.left + offsetX,
            actualWindow.top + offsetY,
            actualWindow.right - actualWindow.left + offsetWidth,
            actualWindow.bottom - actualWindow.top + offsetHeight,
            flags);
    }
}

LRESULT OnHideBorder(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT rcWin;
    RECT rcContent;
    RECT rcStatus;
    Book *pBook = NULL;
    WNDCLASSEX wcex;
    static HICON hIconSm = NULL;
    const UINT windowPosFlags = SWP_NOREDRAW;

    if (hIconSm == NULL)
    {
        GetClassInfoEx(hInst, szWindowClass, &wcex);
        hIconSm = wcex.hIconSm;
    }
    
    if (_WndInfo.status == ds_fullscreen)
        return 0;

    GetClientRectExceptStatusBar(hWnd, &rcWin);
    ClientToScreen(hWnd, reinterpret_cast<POINT*>(&rcWin.left));
    ClientToScreen(hWnd, reinterpret_cast<POINT*>(&rcWin.right));
    rcContent = rcWin;

    SendMessage(hWnd, WM_SETREDRAW, FALSE, 0); // lock redraw
    pBook = _Book;_Book = NULL; // lock onsize
    if (_WndInfo.status == ds_normal) // hide border
    {
        _header->style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE) & (~(WS_CAPTION | WS_THICKFRAME | /*WS_MINIMIZEBOX |*/ WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        _header->exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE) & (~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);
        ShowWindow(_WndInfo.hStatusBar, SW_HIDE);
        SetMenu(hWnd, NULL);
        _WndInfo.bLayered = TRUE;
        _WndInfo.status = ds_borderless;
        SetWindowPos(hWnd, NULL, rcWin.left, rcWin.top, rcWin.right-rcWin.left, rcWin.bottom-rcWin.top, windowPosFlags);
        CorrectWindowContentRect(hWnd, &rcContent, 0, windowPosFlags);
    }
    else if (_WndInfo.status == ds_borderless)// show border
    {
        _header->style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW;
        _header->exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_WINDOWEDGE | WS_EX_ACCEPTFILES | WS_EX_LAYERED;

        GetWindowRect(_WndInfo.hStatusBar, &rcStatus);
        rcWin = rcContent;
        rcWin.bottom += rcStatus.bottom - rcStatus.top;
        AdjustWindowRectEx(&rcWin, _header->style, TRUE, _header->exstyle);

        SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm); // fixed bug
        ShowWindow(_WndInfo.hStatusBar, SW_SHOW);
        SetMenu(hWnd, _WndInfo.hMenu);
        _WndInfo.bLayered = FALSE;
        _WndInfo.status = ds_normal;
        SetWindowPos(hWnd, NULL, rcWin.left, rcWin.top, rcWin.right-rcWin.left, rcWin.bottom-rcWin.top, windowPosFlags);
        CorrectWindowContentRect(hWnd, &rcContent, rcStatus.bottom - rcStatus.top, windowPosFlags);

        if (_menuInvalid)
        {
            _menuInvalid = FALSE;
            OnUpdateMenu(hWnd);
        }
    }
    _Book = pBook;
    
    SendMessage(hWnd, WM_SETREDRAW, TRUE, 0); // lock redraw
    _focusMode = _WndInfo.status != ds_normal; _tocFromFocus = FALSE;
    UpdateReaderLayout(hWnd);
    // repaint for alpha
    Invalidate(hWnd, FALSE, FALSE);
    return 0;
}

LRESULT OnFullScreen(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    MONITORINFO mi;
    RECT rc;

    SendMessage(hWnd, WM_SETREDRAW, FALSE, 0); // lock redraw
    if (_WndInfo.status < ds_fullscreen) // fullscreen
    {
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST),&mi);
        rc = mi.rcMonitor;

        _header->fs_style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);
        _header->fs_exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        GetWindowPlacement(hWnd, &_header->fs_placement);

        if ((WS_MAXIMIZE & _header->fs_style) == WS_MAXIMIZE)
        {
            GetWindowRect(hWnd, &_header->fs_rect);
        }

        _header->style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE) & (~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        _header->exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE) & (~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);
        ShowWindow(_WndInfo.hStatusBar, SW_HIDE);
        SetMenu(hWnd, NULL);
        SetWindowPos(hWnd, NULL, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, SWP_DRAWFRAME);
        _WndInfo.bLayered = TRUE;
        _WndInfo.status = ds_fullscreen;
    }
    else // exit fullscreen
    {
        _header->style = _header->fs_style;
        _header->exstyle = _header->fs_exstyle;
        SetWindowLongPtr(hWnd, GWL_STYLE, _header->style | WS_CLIPCHILDREN);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, _header->exstyle);
        if ((WS_CAPTION & _header->style) == WS_CAPTION)
        {
            ShowWindow(_WndInfo.hStatusBar, SW_SHOW);
            SetMenu(hWnd, _WndInfo.hMenu);
            _WndInfo.bLayered = FALSE;
            _WndInfo.status = ds_normal;
        }
        else
        {
            _WndInfo.bLayered = TRUE;
            _WndInfo.status = ds_borderless;
            if ((WS_MAXIMIZE & _header->fs_style) == WS_MAXIMIZE)
            {
                SetWindowPos(hWnd, NULL, _header->fs_rect.left, _header->fs_rect.top, 
                    _header->fs_rect.right-_header->fs_rect.left, _header->fs_rect.bottom-_header->fs_rect.top, SWP_DRAWFRAME);
            }
        }
        _header->placement = _header->fs_placement;
        SetWindowPlacement(hWnd, &_header->fs_placement);
    }

    SendMessage(hWnd, WM_SETREDRAW, TRUE, 0); // lock redraw
    _focusMode = _WndInfo.status != ds_normal; _tocFromFocus = FALSE;
    UpdateReaderLayout(hWnd);
    // repaint for alpha
    Invalidate(hWnd, FALSE, FALSE);

    return 0;
}

LRESULT OnTopmost(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SetWindowPos(hWnd, _WndInfo.bTopMost ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE);
    _WndInfo.bTopMost = !_WndInfo.bTopMost;
    return 0;
}

LRESULT OnOpenFile(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (IsWindow(_hFindDlg))
    {
        DestroyWindow(_hFindDlg);
        _hFindDlg = NULL;
    }
    TCHAR szFileName[MAX_PATH] = {0};
    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(ofn);  
    ofn.hwndOwner = hWnd;  
#ifdef ENABLE_NETWORK
    ofn.lpstrFilter = _T("Files (*.txt;*.epub;*.mobi;*.azw3;*.azw;*.prc;*.pdb;*.ol;*.web)\0*.txt;*.epub;*.mobi;*.azw3;*.azw;*.prc;*.pdb;*.ol;*.web\0\0");
#else
    ofn.lpstrFilter = _T("Files (*.txt;*.epub;*.mobi;*.azw3;*.azw;*.prc;*.pdb)\0*.txt;*.epub;*.mobi;*.azw3;*.azw;*.prc;*.pdb\0\0");
#endif
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrFile = szFileName; 
    ofn.nMaxFile = sizeof(szFileName)/sizeof(*szFileName);  
    ofn.nFilterIndex = 0;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    BOOL bSel = GetOpenFileName(&ofn);
    if (!bSel)
    {
        return 0;
    }

    OnOpenBook(hWnd, szFileName, FALSE);
    return 0;
}

LRESULT OnAddMark(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int ret;
    if (_Book && _item && !_Book->IsLoading())
    {
        ret = MessageBox_(hWnd, IDS_ADD_BOOKMARK_TIPS, IDS_BOOKMARK, MB_ICONINFORMATION|MB_YESNO);
        if (ret == IDYES)
        {
            int chapter = _Book->GetCurChapterIndex();
            bool relative = _Book->GetBookType() == book_online || _Book->GetBookType() == book_web;
            int position = _item->index;
            bookmark_detail_t detail;
            if (chapter >= 0 && chapter < (int)_Book->GetChapters()->size())
            {
                const chapter_item_t& entry = (*_Book->GetChapters())[chapter];
                detail.title = ReaderClipText(entry.title, 255);
                if (relative && entry.index >= 0) { detail.chapter = chapter; position -= entry.index; }
            }
            detail.position = position; detail.summary = BookmarkSummary(_item->index);
            if (_Cache.add_mark(_item, position, &detail))
            {
                OnUpdateBookMark(hWnd);
                Save(hWnd);
            }
            else MessageBox(hWnd, _T("该位置已有书签，或书签已达到容量限制。"), _T("书签"), MB_OK | MB_ICONINFORMATION);
        }
    }
    return 0;
}

LRESULT OnAutoPage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
    {
        if (_IsAutoPage)
        {
            StopAutoPage(hWnd);
        }
        else
        {
            StartAutoPage(hWnd);
        }
    }
    return 0;
}

LRESULT OnSearch(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
        OnFindText(hWnd, message, wParam, lParam);
    return 0;
}

LRESULT OnJump(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
        DialogBox(hInst, MAKEINTRESOURCE(IDD_JUMP_PROGRESS), hWnd, JumpProgress);
    return 0;
}

int Editwordbreakproc(
    LPTSTR lpch,
    int ichCurrent,
    int cch,
    int code
    )
{
    if (WB_ISDELIMITER == code && cch == 0x0a)
    {
        return TRUE;
    }
    return FALSE;
}

LRESULT OnEditMode(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR *text = NULL;
    BOOL readonly = TRUE;

    if (EC_IsEditMode())
    {
        EC_LeaveEditMode();
        return 0;
    }

    if (_Book && !_Book->IsLoading())
    {
        if (!_Book->IsCoverPage() && _Book->GetCurPageText(&text))
        {
            _WndInfo.status_bak = _WndInfo.status;
            if (_WndInfo.status == ds_fullscreen)
            {
                OnFullScreen(hWnd, message, wParam, lParam);
            }

            if (_WndInfo.status == ds_borderless)
            {
                OnHideBorder(hWnd, message, wParam, lParam);
            }

            if (_Book->GetBookType() == book_text)
            {
                readonly = FALSE;
            }

            EC_EnterEditMode(hInst, hWnd, &_header->font, text, readonly);
            UpdateReaderLayout(hWnd);

            free(text);
        }
    }

    return 0;
}

LRESULT OnPageUp(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
    {
        _Book->PageUp(hWnd);
    }
    return 0;
}

LRESULT OnPageDown(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
    {
        _Book->PageDown(hWnd);
    }
    return 0;
}

LRESULT OnLineUp(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
    {
        _Book->LineUp(hWnd);
    }
    return 0;
}

LRESULT OnLineDown(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (_Book && !_Book->IsLoading())
    {
        _Book->LineDown(hWnd);
    }
    return 0;
}

LRESULT OnChapterUp(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (BeginReadingJump())
    {
        _Book->JumpPrevChapter(hWnd);
        _Book->ResetLayout();
        Save(hWnd);
    }
    return 0;
}

LRESULT OnChapterDown(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (BeginReadingJump())
    {
        _Book->JumpNextChapter(hWnd);
        _Book->ResetLayout();
        Save(hWnd);
    }
    return 0;
}

LRESULT OnFontZoomIn(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    //const int ZOOM_MIN = 1;
    const int ZOOM_MAX = 72;
    int PointSize;

    if (_header && _Book && !_Book->IsLoading())
    {
        PointSize = abs(MulDiv(_header->font.lfHeight, 72, GetDeviceCaps(GetDC(NULL), LOGPIXELSY)));
        if (PointSize < ZOOM_MAX)
        {
            PointSize++;
            _header->font.lfHeight = -MulDiv(PointSize, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
            _Book->ReDraw(hWnd);
            Save(hWnd);
        }
    }
    return 0;
}

LRESULT OnFontZoomOut(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const int ZOOM_MIN = 1;
    //const int ZOOM_MAX = 72;
    int PointSize;

    if (_header && _Book && !_Book->IsLoading())
    {
        PointSize = abs(MulDiv(_header->font.lfHeight, 72, GetDeviceCaps(GetDC(NULL), LOGPIXELSY)));
        if (PointSize > ZOOM_MIN)
        {
            PointSize--;
            _header->font.lfHeight = -MulDiv(PointSize, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
            _Book->ReDraw(hWnd);
            Save(hWnd);
        }
    }
    return 0;
}

LRESULT OnDropFiles(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HDROP hDropInfo = (HDROP)wParam;
    UINT  nFileCount = ::DragQueryFile(hDropInfo, (UINT)-1, NULL, 0);
    TCHAR szFileName[MAX_PATH] = _T("");
    DWORD dwAttribute;
    TCHAR *ext = NULL;

    for (UINT i = 0; i < nFileCount; i++)
    {
        ::DragQueryFile(hDropInfo, i, szFileName, sizeof(szFileName));
        dwAttribute = ::GetFileAttributes(szFileName);

        if (dwAttribute & FILE_ATTRIBUTE_DIRECTORY)
        {          
            continue;
        }

        // Check the same formats accepted by dialog, command line and history.
        ext = PathFindExtension(szFileName);
        if (!IsSupportedBookExtension(ext))
        {
            continue;
        }

        // open file
        OnOpenBook(hWnd, szFileName, FALSE);

        // just open first file
        break;
    }

    ::DragFinish(hDropInfo);
    return 0;
}

LRESULT OnFindText(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    OpenReaderSearch(hWnd);
    return 0;
}

LRESULT OnUpdateChapters(HWND hWnd)
{
    chapters_t *chapters;
    int i;

    TVITEM tvi = {0};
    TVINSERTSTRUCT tvins = {0};
    HTREEITEM hPrev = (HTREEITEM)TVI_FIRST;
    
    UINT token = _Book ? _Book->GetOpenToken() : 0;
    std::vector<std::wstring> titles;
    if (_Book) for (const auto& chapter : *_Book->GetChapters()) titles.push_back(chapter.title);
    if (token == _chapterBookToken && titles == _chapterTitles) { SyncCurrentChapter(); return 0; }
    _chapterBookToken = token; _chapterTitles.swap(titles);
    ThemeSetCurrentChapter(_hTreeView, NULL);
    _syncingToc = TRUE;
    _chapterItems.clear(); _tocCurrent = -1;
    bool visible = IsWindowVisible(_hTreeView) != FALSE;
    if (visible) SendMessage(_hTreeView, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(_hTreeView);
    if (_Book)
    {
        tvi.mask = TVIF_TEXT /*| TVIF_IMAGE | TVIF_SELECTEDIMAGE */| TVIF_PARAM;

        chapters = _Book->GetChapters();
        for (i = 0; i < (int)chapters->size(); i++)
        {
            tvi.pszText = (TCHAR*)(*chapters)[i].title.c_str();
            tvi.cchTextMax = sizeof(tvi.pszText) / sizeof(tvi.pszText[0]);
            tvi.lParam = (LPARAM)i;
            tvins.item = tvi;
            tvins.hInsertAfter = hPrev;
            tvins.hParent = TVI_ROOT;

            // Add the item to the tree-view control. 
            hPrev = (HTREEITEM)SendMessage(_hTreeView, TVM_INSERTITEM, 0, (LPARAM)(LPTVINSERTSTRUCT)&tvins);
            _chapterItems.push_back(hPrev);
        }
    }

    if (visible) { SendMessage(_hTreeView, WM_SETREDRAW, TRUE, 0); InvalidateRect(_hTreeView, NULL, FALSE); }
    _syncingToc = FALSE;
    SyncCurrentChapter();
    return 0;
}

LRESULT OnUpdateBookMark(HWND hWnd)
{
    UINT token = _Book ? _Book->GetOpenToken() : 0;
    std::vector<std::wstring> labels;
    if (_Book && !_Book->IsLoading() && _item)
        for (int i = 0; i < _item->mark_size; ++i)
        {
            bookmark_detail_t detail = DescribeBookmark(i);
            labels.push_back(detail.title + L" · " + detail.summary);
        }
    if (token == _markBookToken && labels == _markLabels) return 0;
    _markBookToken = token; _markLabels = labels;
    bool visible = IsWindowVisible(_hTreeMark) != FALSE;
    if (visible) SendMessage(_hTreeMark, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(_hTreeMark);
    for (size_t i = 0; i < labels.size(); ++i)
    {
        TVINSERTSTRUCT insert = {}; insert.hParent = TVI_ROOT; insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM; insert.item.pszText = (TCHAR*)labels[i].c_str(); insert.item.lParam = i;
        TreeView_InsertItem(_hTreeMark, &insert);
    }
    if (visible) { SendMessage(_hTreeMark, WM_SETREDRAW, TRUE, 0); InvalidateRect(_hTreeMark, NULL, FALSE); }
    return 0;
}

LRESULT OnOpenBookResult(HWND hWnd, BOOL result)
{
    item_t *item = NULL;
    UINT type;
    UINT errorMessage;
    TCHAR fileName[MAX_PATH] = { 0 };
    
    //EnableWindow(hWnd, TRUE);
    if (!result)
    {
        StopLoadingImage(hWnd);
        _tcscpy(fileName, _Book->GetFileName());
        type = (_Book->GetBookType() == book_online || _Book->GetBookType() == book_web) ? MB_RETRYCANCEL : MB_OK;
        // Read parser-specific errors before destroying the failed book.
        errorMessage = _Book->GetOpenErrorMessage();
        if (errorMessage == 0)
            errorMessage = IDS_OPEN_FILE_FAILED;
        delete _Book;
        _Book = NULL;
        if (IDRETRY == MessageBox_(hWnd, errorMessage, IDS_ERROR, type | MB_ICONERROR))
        {
            OnOpenBook(hWnd, fileName, FALSE);
        }
        // Update title
        UpdateTitle(hWnd);
        Invalidate(hWnd, TRUE, FALSE);
        return FALSE;
    }

    // create new item
    TCHAR key[64]; _Cache.file_key(_Book->GetFileName(), key);
    item = _Cache.find_item(_Book->GetFileName(), key);
    if (NULL == item)
    {
        item = _Cache.new_item(_Book->GetFileName(), key);
        _header = _Cache.get_header(); // after realloc the header address has been changed
    }

    if (!item) { StopLoadingImage(hWnd); return FALSE; }
    const int openedId = item->id;
    _Cache.relink_item(openedId, _Book->GetFileName(), key);
    // open item
    _item = _Cache.open_item(openedId);
    _activeBookId = openedId;
    _readingHistory.Clear(); _jumpPending = _restorePending = FALSE;
    _item->last_read = (u64)time(NULL);
    _item->text_length = _Book->GetTextLength();
    if (!_item->read_state) _item->read_state = 1;
    LibraryChanged();

    // set param
    _header = _Cache.get_header();
    _Book->Init(&_item->index, _header);
    _Book->SetChapterRule(&(_header->chapter_rule));

#ifdef ENABLE_NETWORK
    if (_Book->GetBookType() == book_online)
    {
        _item->is_new = FALSE;
        ((OnlineBook*)_Book)->UpdateBookSource();
    }
#endif

    InvalidateReadingText();
    // update menu
    OnUpdateMenu(hWnd);

    // Update title
    UpdateTitle(hWnd);

    // update chapter
    PostMessage(hWnd, WM_UPDATE_CHAPTERS, 0, NULL);

    // save
    Save(hWnd);

    // stoploading & repaint
    StopLoadingImage(hWnd);

    return 0;
}

LRESULT OnCopyData(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    COPYDATASTRUCT *pCopyData = (COPYDATASTRUCT*)lParam;
    TCHAR fullname[MAX_PATH];

    switch (pCopyData->dwData)
    {
    case 0: // extern open file
        GetFullPathName((TCHAR*)pCopyData->lpData, MAX_PATH, fullname, NULL);
        // check if file vaild
        if (IsVaildFile(NULL, fullname, NULL))
        {
            OnOpenBook(hWnd, fullname, FALSE);
        }
        break;
    }

    return 0;
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    LPMOUSEHOOKSTRUCT mh = NULL;
    RECT rect;

    if (nCode == HC_ACTION)
    {
        switch (wParam)
        {
        case WM_MOUSEMOVE:
            mh = (LPMOUSEHOOKSTRUCT)lParam;
            GetWindowRect(_hWnd, &rect);
            if (PtInRect(&rect, mh->pt))
            {
                if (!IsWindowVisible(_hWnd))
                    ShowWindow(_hWnd, SW_SHOW);
            }
            else
            {
                if (IsWindowVisible(_hWnd))
                    ShowWindow(_hWnd, SW_HIDE);
            }
            break;
        default:
            break;
        }
    }
    
    return CallNextHookEx(_hMouseHook, nCode, wParam, lParam);
}

#if ENABLE_GLOBAL_KEY
extern BOOL IsCoveredByOtherWindow(HWND hWnd);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    KBDLLHOOKSTRUCT const * pData;
    if (nCode == HC_ACTION)
    {
        if (wParam == WM_KEYDOWN)
        {
            do
            {
                if (IsCoveredByOtherWindow(_hWnd))
                    break;

                pData = (KBDLLHOOKSTRUCT*)lParam;
                if (KS_KeyDownProc(_hWnd, WM_KEYDOWN, pData->vkCode, 0))
                    break;

#if 1
                if (VK_ESCAPE == pData->vkCode)
                {
                    if (_WndInfo.status == ds_fullscreen)
                    {
                        OnFullScreen(_hWnd, WM_KEYDOWN, pData->vkCode, lParam);
                    }
                }
                else if ('P' == pData->vkCode)
                {
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000
                        && GetAsyncKeyState(VK_SHIFT) & 0x8000
                        && GetAsyncKeyState(18) & 0x8000) // alt
                    {
                        if (!_hMouseHook)
                            _hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, hInst, NULL);
                        else
                        {
                            UnhookWindowsHookEx(_hMouseHook);
                            _hMouseHook = NULL;
                        }
                    }
                }
#ifdef ENABLE_NETWORK
                else if (VK_F5 == pData->vkCode) // online book manual check
                {
                    if (_Book && !_Book->IsLoading() && _Book->GetBookType() == book_online)
                    {
                        if (IDYES == MessageBox_(_hWnd, IDS_REFRESH_TIP, IDS_WARN, MB_ICONINFORMATION | MB_YESNO))
                        {
                            chkbook_arg_t* arg = GetCheckBookArguments();
                            arg->book = (OnlineBook*)_Book;
                            arg->hWnd = _hWnd;
                            arg->checked_list.insert(_Book->GetFileName());
                            if (2 != ((OnlineBook*)_Book)->ManualCheckUpdate(_hWnd, OnManualCheckBookUpdateCallback, arg))
                            {
                                if (arg->book != _Book)
                                    delete arg->book;
                                arg->book = NULL;
                            }
                        }
                    }
                }
#endif
#endif
            } while (0);
        }
    }
    return CallNextHookEx(_hKeyboardHook, nCode, wParam, lParam);
}
#endif

void ShowHideWindow(HWND hWnd)
{
    // show or hide window
    BOOL isShow = IsWindowVisible(hWnd);
    ShowWindow(hWnd, isShow ? SW_HIDE : SW_SHOW);
    if (!isShow)
    {
        if (IsIconic(hWnd))
            ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
    }
}

BOOL IsVaildFile(HWND hWnd, TCHAR *filename, int *p_size)
{
    TCHAR *ext = NULL;
    FILE *fp = NULL;
    int size = 0;

    if (p_size) *p_size = 0;
    ext = PathFindExtension(filename);

#ifdef ENABLE_NETWORK
    if (!_header || _header->book_source_count == 0)
    {
        if (0 == _tcsicmp(ext, _T(".ol")))
        {
            if (hWnd)
                MessageBox_(hWnd, IDS_NO_BOOKSOURCE_FAIL, IDS_ERROR, MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }
#endif
    if (!IsSupportedBookExtension(ext))
    {
        if (hWnd)
            MessageBox_(hWnd, IDS_UNKNOWN_FORMAT, IDS_ERROR, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    fp = _tfopen(filename, _T("rb"));
    if (!fp)
    {
        if (hWnd)
            MessageBox_(hWnd, IDS_OPEN_FILE_FAILED, IDS_ERROR, MB_OK | MB_ICONERROR);
        return FALSE;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);

    if (size == 0)
    {
        if (hWnd)
            MessageBox_(hWnd, IDS_EMPTY_FILE, IDS_ERROR, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (p_size) *p_size = size;
    return TRUE;
}

void OnOpenBook(HWND hWnd, TCHAR *filename, BOOL forced)
{
    item_t *item = NULL;
    TCHAR *ext = NULL;
    int size = 0;
    TCHAR szFileName[MAX_PATH] = {0};
#ifdef ENABLE_NETWORK
    chkbook_arg_t* arg = NULL;
#endif

    if (IsWindow(_hFindDlg)) { DestroyWindow(_hFindDlg); _hFindDlg = NULL; }
    _tcscpy(szFileName, filename);
    ext = PathFindExtension(szFileName);

    if (!IsVaildFile(hWnd, szFileName, &size))
    {
        return;
    }

    // check md5, is already exist
    item = _Cache.find_item(szFileName);
    if (item)
    {
        if (!forced)
        {
            if (_item && item->id == _item->id && _Book && !_Book->IsLoading()) // current is opened
            {
                OnUpdateMenu(hWnd);
                return;
            }
        }
    }
    Save(hWnd);
    _item = NULL; _activeBookId = -1;
    _jumpPending = _restorePending = FALSE;

    if (_Book)
    {
        delete _Book;
        _Book = NULL;
    }

    InvalidateReadingText();
    OnUpdateChapters(hWnd); OnUpdateBookMark(hWnd);

    if (_tcsicmp(ext, _T(".txt")) == 0)
    {
        _Book = new TextBook;
        _Book->SetFileName(szFileName);
        _Book->SetChapterRule(&(_header->chapter_rule));
        _Book->OpenBook(NULL, size, hWnd);
    }
    else if (_tcsicmp(ext, _T(".epub")) == 0)
    {
        _Book = new EpubBook;
        _Book->SetFileName(szFileName);
        _Book->OpenBook(hWnd);
    }
    // All libmobi aliases share the same validation and parser dispatch.
    else if (IsMobiBookExtension(ext))
    {
        _Book = new MobiBook;
        _Book->SetFileName(szFileName);
        _Book->OpenBook(hWnd);
    }
#ifdef ENABLE_NETWORK
    else if (_tcsicmp(ext, _T(".ol")) == 0)
    {
        arg = GetCheckBookArguments();
        if (arg && arg->book && arg->book->GetBookType() == book_online
            && 0 == _tcscmp(arg->book->GetFileName(), szFileName))
        {
            if (arg->book != _Book)
                delete arg->book;
            arg->book = NULL;
        }
        _Book = new OnlineBook;
        _Book->SetFileName(szFileName);
        _Book->OpenBook(NULL, size, hWnd);
    }
    else if (_tcsicmp(ext, _T(".web")) == 0)
    {
        _Book = new WebBook;
        _Book->SetFileName(szFileName);
        _Book->OpenBook(NULL, size, hWnd);
    }
#endif

    //EnableWindow(hWnd, FALSE);
    PlayLoadingImage(hWnd);
}

#ifdef ENABLE_NETWORK
void OnOpenOlBook(HWND hWnd, void* olparam)
{
    static TCHAR oldir[MAX_PATH] = { 0 };
    TCHAR savepath[MAX_PATH] = { 0 };
    ol_book_param_t* param = (ol_book_param_t*)olparam;
    ol_header_t olheader = { 0 };
    FILE* fp = NULL;
    int i;
    int ret;

    // create save path
    if (!oldir[0])
    {
        // generate online file save path
        GetModuleFileName(NULL, oldir, sizeof(TCHAR) * (MAX_PATH - 1));
        for (i = (int)_tcslen(oldir) - 1; i >= 0; i--)
        {
            if (oldir[i] == _T('\\') || oldir[i] == _T('/'))
            {
                memcpy(&oldir[i + 1], ONLINE_FILE_SAVE_PATH, (_tcslen(ONLINE_FILE_SAVE_PATH) + 1) * sizeof(TCHAR));
                break;
            }
        }

        // create path
        if (CreateDirectory(oldir, NULL))
        {
            SetFileAttributes(oldir, FILE_ATTRIBUTE_HIDDEN);
        }
    }
    _tcscpy(savepath, oldir);
    _tcscat(savepath, param->book_name);
    _tcscat(savepath, _T(".ol"));

    // check .ol file is exist
    if (PathFileExists(savepath))
    {
        ret = MessageBox_(hWnd, IDS_FILE_ISEXIST, IDS_WARN, MB_ICONWARNING | MB_YESNOCANCEL);
        if (ret == IDCANCEL)
        {
            return;
        }
        if (ret == IDNO)
        {
            OnOpenBook(hWnd, savepath, FALSE);
            return;
        }

        DeleteFile(savepath);

        // Replacing an online descriptor keeps its stable library record and marks.
    }

    // generate ol_header_t
    olheader.header_size = sizeof(ol_header_t) - sizeof(ol_chapter_info_t);
    olheader.book_name_offset = olheader.header_size;
    olheader.header_size += (u32)((_tcslen(param->book_name) + 1) * sizeof(TCHAR));
    olheader.main_page_offset = olheader.header_size;
    olheader.header_size += (u32)((strlen(param->main_page) + 1) * sizeof(char));
    olheader.host_offset = olheader.header_size;
    olheader.header_size += (u32)((strlen(param->host) + 1) * sizeof(char));
    olheader.update_time = 0;
    olheader.is_finished = param->is_finished;
    olheader.chapter_size = 0;

    // write file
    fp = _tfopen(savepath, _T("wb"));
    fwrite(&olheader, 1, olheader.book_name_offset, fp);
    fwrite(param->book_name, 1, olheader.main_page_offset - olheader.book_name_offset, fp);
    fwrite(param->main_page, 1, olheader.host_offset - olheader.main_page_offset, fp);
    fwrite(param->host, 1, olheader.header_size - olheader.host_offset, fp);
    fclose(fp);

    OnOpenBook(hWnd, savepath, FALSE);
}

void UpdateBookMark(HWND hWnd, int index, int size)
{
    int i;
    if (!_item || !_Book || _tcscmp(_item->file_name, _Book->GetFileName()) != 0)
    {
        return;
    }

    _Cache.rebase_marks(_item, index, size);
}
#endif

VOID GetCacheVersion(TCHAR *ver)
{
    TCHAR version[256] = { 0 };
    GetApplicationVersion(version);
    wcscpy(ver, version);
}

BOOL Init(void)
{
    if (!_Cache.init())
    {
        MessageBox(NULL, _T("无法读取书库缓存。原 .cache.dat 已保留；请先备份当前文件，再检查同目录的 .cache.dat.bak 或 .cache.dat.v1.bak。损坏或较新版本的缓存不会被覆盖。"), _T("书库缓存读取失败"), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    _header = _Cache.get_header();
    ThemeConfigure(_header);

#ifdef ENABLE_NETWORK
    hapi_init();
#if TEST_MODEL
    logger_create();
    hapi_set_logger(logger_printf);
#endif
    // set proxy for http client
    hapi_set_proxy_ex(&_header->proxy);
#endif

    // just for debug
#if 0
    extern void TestXpathFromDump(void);
    TestXpathFromDump();
#endif

    return TRUE;
}

void Exit(void)
{
    if (_Book)
    {
        delete _Book;
        _Book = NULL;
    }

    if (!_Cache.exit())
    {
        MessageBox_(NULL, IDS_SAVE_CACHE_FAIL, IDS_ERROR, MB_OK);
    }
#ifdef ENABLE_NETWORK
    HtmlParser::ReleaseInstance();
    hapi_uninit();
#if TEST_MODEL
    logger_destroy();
#endif
#endif
}

static void _free_resource(HWND hWnd)
{
    CancelSplitter(hWnd);
    CloseReaderSearch();
    CloseLibrary();
    if (_readerUIFont) { DeleteObject(_readerUIFont); _readerUIFont = NULL; }
    // stop loading
    StopLoadingImage(hWnd);
    if (_loading)
    {
        if (_loading->hMemory)
            ::GlobalFree(_loading->hMemory);
        if (_loading->image)
            delete _loading->image;
        free(_loading->item);
        free(_loading);
        _loading = NULL;
    }
    // close book
    if (_Book)
    {
        delete _Book;
        _Book = NULL;
    }
    _readingSurface.Reset(); _frameSurface.Reset();
    ThemeShutdown();
    // reset
    ShowSysTray(hWnd, FALSE);
    StopAutoPage(hWnd);
    KS_UnRegisterAllHotKey(hWnd);
    if (_hMouseHook)
    {
        UnhookWindowsHookEx(_hMouseHook);
        _hMouseHook = NULL;
    }
#if ENABLE_GLOBAL_KEY
    if (_hKeyboardHook)
    {
        UnhookWindowsHookEx(_hKeyboardHook);
        _hKeyboardHook = NULL;
    }
#endif
}

static void _update_data(HWND hWnd, BOOL keep_header, BOOL do_save)
{
    RECT rcbak1 = {0}, rcbak2 = {0};
    LOGFONT fontbak = {0};
#if ENABLE_TAG
    LOGFONT tagfontbak[MAX_TAG_COUNT] = { 0 };
#endif

    // backup
    GetWindowPlacement(hWnd, &_header->placement);
    if (keep_header)
    {
        rcbak1 = _header->placement.rcNormalPosition;
        rcbak2 = _header->fs_placement.rcNormalPosition;
        fontbak = _header->font;
    }

    RestoreRectForDpi(&_header->placement.rcNormalPosition);
    RestoreRectForDpi(&_header->fs_placement.rcNormalPosition);
    RestoreFontForDpi(&_header->font);
#if ENABLE_TAG
    for (int i = 0; i < MAX_TAG_COUNT; i++)
    {
        tagfontbak[i] = _header->tags[i].font;
        RestoreFontForDpi(&_header->tags[i].font);
    }
#endif
    _header->style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);
    _header->exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE);

    // save
    BOOL saved = !do_save || _Cache.save();

    // restore
    if (keep_header)
    {
        _header->placement.rcNormalPosition = rcbak1;
        _header->fs_placement.rcNormalPosition = rcbak2;
        _header->font = fontbak;
#if ENABLE_TAG
        for (int i = 0; i < MAX_TAG_COUNT; i++)
        {
            _header->tags[i].font = tagfontbak[i];
        }
#endif
    }
    static BOOL warned = FALSE;
    if (!saved && !warned)
    {
        warned = TRUE;
        MessageBox_(hWnd, IDS_SAVE_CACHE_FAIL, IDS_ERROR, MB_OK | MB_ICONERROR);
    }
    else if (saved && do_save) warned = FALSE;
}

void Save(HWND hWnd)
{
#if ENABLE_REALTIME_SAVE
    PostMessage(hWnd, WM_SAVE_CACHE, 0, NULL);
#endif
}

LRESULT OnSave(HWND hWnd)
{
    static ReadingAnchor observed;
    if (_Book && _item && !_Book->IsLoading())
    {
        ReadingAnchor current = CurrentReadingAnchor();
        if (!(current == observed)) { _item->last_read = (u64)time(NULL); observed = current; }
    }
    LibraryChanged();
#if ENABLE_REALTIME_SAVE
    _update_data(hWnd, TRUE, TRUE);
#endif
    return 0;
}

static void SetProgressText(const TCHAR* text)
{
    TCHAR old[256] = {}; SendMessage(_WndInfo.hStatusBar, SB_GETTEXT, 0, (LPARAM)old);
    if (_tcscmp(old, text)) SendMessage(_WndInfo.hStatusBar, SB_SETTEXT, 0, (LPARAM)text);
}

void UpdateProgess(void)
{
    TCHAR progress[256] = {0};
    TCHAR str[256] = { 0 };
    double dprog = 0.0;
    int nprog = 0;

    if (EC_IsEditMode())
    {
        LoadString(hInst, IDS_EDIT_MODE_TIPS, progress, 256);
        SetProgressText(progress);
        return;
    }

    if (_item && _Book && !_Book->IsLoading())
    {
        _item->text_length = _Book->GetTextLength();
        static ReaderProgressObservation observed;
        if (observed.MarkRead(_Book->GetOpenToken(), _item->index,
            _Book->GetBookType() != book_online && _Book->GetBookType() != book_web, _Book->IsLastPage()))
            _item->read_state = 2;
        dprog = (double)_Book->GetProgress();
        nprog = (int)(dprog * 100);
        dprog = (double)nprog / 100.0;
        if (!_IsAutoPage)
        {
            _stprintf(progress, _T("  %.2f%%  ( %d / %d )"), dprog, _item->index + _Book->GetPageLength(), _Book->GetTextLength());
        }
        else
        {
            LoadString(hInst, IDS_AUTOPAGING, str, 256);
            _stprintf(progress, _T("  %.2f%%  ( %d / %d )  [%s]"), dprog, _item->index + _Book->GetPageLength(), _Book->GetTextLength(), str);
        }
        SetProgressText(progress);
    }
    else
    {
        SetProgressText(_T(""));
    }
}

void UpdateTitle(HWND hWnd)
{
    TCHAR szOldTitle[MAX_LOADSTRING + MAX_PATH + MAX_PATH] = { 0 };
    TCHAR szNewTitle[MAX_LOADSTRING + MAX_PATH + MAX_PATH] = { 0 };
    TCHAR szFileName[MAX_PATH] = { 0 };
    TCHAR szChapter[MAX_PATH] = { 0 };

    if (_Book)
    {
        memcpy(szFileName, _Book->GetFileName(), _tcslen(_Book->GetFileName()+1) * sizeof(TCHAR));
        PathRemoveExtension(szFileName);
        if (_Book->IsLoading())
        {
            _stprintf(szNewTitle, _T("%s - %s"), szTitle, PathFindFileName(szFileName));
        }
        else
        {
            if (_Book->GetChapterTitle(szChapter, MAX_PATH))
            {
                _stprintf(szNewTitle, _T("%s - %s - %s"), szTitle, PathFindFileName(szFileName), szChapter);
            }
            else
            {
                _stprintf(szNewTitle, _T("%s - %s"), szTitle, PathFindFileName(szFileName));
            }
        }
    }
    else
    {
        _stprintf(szNewTitle, _T("%s"), szTitle);
    }

    GetWindowText(hWnd, szOldTitle, MAX_LOADSTRING + MAX_PATH + MAX_PATH - 1);
    if (_tcscmp(szOldTitle, szNewTitle))
        SetWindowText(hWnd, szNewTitle);
}

void RemoveMenuById(HMENU hMenu, BOOL separator, int id)
{
    HMENU hSubMenu;
    int count;
    int i,j;
    int menuid;

    if (!hMenu)
        return;

    count = GetMenuItemCount(hMenu);
    for (i=0; i<count; i++)
    {
        hSubMenu = GetSubMenu(hMenu, i);
        if (hSubMenu)
            RemoveMenuById(hSubMenu, separator, id);
        menuid = GetMenuItemID(hMenu, i);
        if (id == menuid)
        {
            if (separator)
            {
                j = i+1;
                RemoveMenu(hMenu, j, MF_BYPOSITION);
            }
            RemoveMenu(hMenu, id, MF_BYCOMMAND);
            return;
        }
    }
}

void RemoveMenus(HWND hWnd, BOOL redraw)
{
    HMENU hMenu;

    hMenu = GetMenu(hWnd);

#ifndef ENABLE_NETWORK
    RemoveMenuById(hMenu, FALSE, IDM_WEB);      // 必须在 IDM_ONLINE 之前（TRUE 会连带删除后面一项）
    RemoveMenuById(hMenu, TRUE, IDM_PROXY);
    RemoveMenuById(hMenu, TRUE, IDM_ONLINE);
#endif

#if !ENABLE_TAG
    RemoveMenuById(hMenu, FALSE, IDM_TAGSET);
#endif

    if (redraw)
        DrawMenuBar(hWnd);
}

BOOL GetClientRectExceptStatusBar(HWND hWnd, RECT* rc)
{
    RECT rect;
    GetClientRect(hWnd, rc);
    if (IsWindowVisible(_WndInfo.hStatusBar))
    {
        GetClientRect(_WndInfo.hStatusBar, &rect);
        rc->bottom -= rect.bottom;
    }
    return TRUE;
}

void WheelSpeedInit(HWND hDlg)
{
    HWND hWnd = NULL;
    TCHAR szText[MAX_LOADSTRING] = {0};

    hWnd = GetDlgItem(hDlg, IDC_COMBO_SPEED);
    LoadString(hInst, IDS_SINGLE_LINE, szText, MAX_LOADSTRING);
    SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)szText);
    LoadString(hInst, IDS_DOUBLE_LINE, szText, MAX_LOADSTRING);
    SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)szText);
    LoadString(hInst, IDS_THREE_LINE, szText, MAX_LOADSTRING);
    SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)szText);
    LoadString(hInst, IDS_FULL_PAGE, szText, MAX_LOADSTRING);
    SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)szText);

    if (_header->wheel_speed > ws_fullpage)
        _header->wheel_speed = ws_fullpage;

    SendMessage(hWnd, CB_SETCURSEL, _header->wheel_speed, NULL);
}

Gdiplus::Bitmap* LoadBGImage(int w, int h, BYTE alpha)
{
    static Gdiplus::Bitmap *bgimg = NULL;
    static TCHAR curfile[MAX_PATH] = {0};
    static int curWidth = 0;
    static int curHeight = 0;
    static int curmode = Stretch;
    static BYTE s_alpha = 0xFF;
    Gdiplus::Bitmap *image = NULL;
    Gdiplus::Graphics *graphics = NULL;
    Gdiplus::ImageAttributes ImgAtt;
    Gdiplus::RectF rcDrawRect;

    if (!ThemeReadingColors(_header).image || !_header->bg_image.file_name[0])
    {
        return NULL;
    }

    if (_tcscmp(_header->bg_image.file_name, curfile) == 0 && curWidth == w && curHeight == h 
        && curmode == _header->bg_image.mode && bgimg && alpha == s_alpha)
    {
        return bgimg;
    }

    if (!FileExists(_header->bg_image.file_name))
    {
        _header->bg_image.file_name[0] = 0;
        return NULL;
    }
    
    if (bgimg)
    {
        delete bgimg;
        bgimg = NULL;
    }
    _tcscpy(curfile, _header->bg_image.file_name);
    curWidth = w;
    curHeight = h;
    curmode = _header->bg_image.mode;
    s_alpha = alpha;
    
    // load image file
    image = Gdiplus::Bitmap::FromFile(curfile);
    if (image == NULL)
        return NULL;
    if (Gdiplus::Ok != image->GetLastStatus())
    {
        delete image;
        image = NULL;
        return NULL;
    }

    // create bg image
    bgimg = new Gdiplus::Bitmap(curWidth, curHeight, PixelFormat32bppARGB);
    rcDrawRect.X=0.0;
    rcDrawRect.Y=0.0;
    rcDrawRect.Width=(float)curWidth;
    rcDrawRect.Height=(float)curHeight;
    graphics = Gdiplus::Graphics::FromImage(bgimg);
    graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    Gdiplus::ColorMatrix colorMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alpha/255.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    ImgAtt.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
    switch (curmode)
    {
    case Stretch:
        graphics->DrawImage(image,rcDrawRect,0.0,0.0,(float)image->GetWidth(),(float)image->GetHeight(), Gdiplus::UnitPixel,&ImgAtt);
        break;
    case Tile:
        ImgAtt.SetWrapMode(Gdiplus::WrapModeTile);
        graphics->DrawImage(image,rcDrawRect,0.0,0.0,(float)curWidth,(float)curHeight, Gdiplus::UnitPixel,&ImgAtt);
        break;
    case TileFlip:
        ImgAtt.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        graphics->DrawImage(image,rcDrawRect,0.0,0.0,(float)curWidth,(float)curHeight, Gdiplus::UnitPixel,&ImgAtt);
        break;
    default:
        graphics->DrawImage(image,rcDrawRect,0.0,0.0,(float)image->GetWidth(),(float)image->GetHeight(), Gdiplus::UnitPixel,&ImgAtt);
        break;
    }

    delete image;
    delete graphics;
    return bgimg;
}

BOOL FileExists(TCHAR *file)
{
    DWORD dwAttrib = GetFileAttributes(file);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
        !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void StartAutoPage(HWND hWnd)
{
    if (_item && !_IsAutoPage && _Book && !_Book->IsLoading())
    {
        if ((_header->autopage_mode & 0xf0) == apm_count)
        {
            SetTimer(hWnd, IDT_TIMER_PAGE, _header->uElapse * _Book->GetPageLength(), NULL);
        }
        else
        {
            SetTimer(hWnd, IDT_TIMER_PAGE, _header->uElapse, NULL);
        }
        _IsAutoPage = TRUE;
        UpdateProgess();
    }
}

void StopAutoPage(HWND hWnd)
{
    if (_IsAutoPage)
    {
        KillTimer(hWnd, IDT_TIMER_PAGE);
        _IsAutoPage = FALSE;
        UpdateProgess();
    }
}

void PauseAutoPage(HWND hWnd)
{
    if (_IsAutoPage)
    {
        KillTimer(hWnd, IDT_TIMER_PAGE);
    }
}

void ResumeAutoPage(HWND hWnd)
{
    if (_item && _IsAutoPage && _Book && !_Book->IsLoading())
    {
        if ((_header->autopage_mode & 0xf0) == apm_count)
        {
            SetTimer(hWnd, IDT_TIMER_PAGE, _header->uElapse * _Book->GetPageLength(), NULL);
        }
        else
        {
            SetTimer(hWnd, IDT_TIMER_PAGE, _header->uElapse, NULL);
        }
    }
    else
    {
        KillTimer(hWnd, IDT_TIMER_PAGE);
        _IsAutoPage = FALSE;
    }
}

void ResetAutoPage(HWND hWnd)
{
    if (_item && _IsAutoPage && _Book && !_Book->IsLoading())
    {
        if ((_header->autopage_mode & 0xf0) == apm_count)
        {
            KillTimer(hWnd, IDT_TIMER_PAGE);
            SetTimer(hWnd, IDT_TIMER_PAGE, _header->uElapse * _Book->GetPageLength(), NULL);
        }
    }
}

#ifdef ENABLE_NETWORK
void CheckUpgrade(HWND hWnd)
{
    // set proxy & check upgrade
    _Upgrade.SetIngoreVersion(_header->ingore_version);
    _Upgrade.SetCheckVerTime(&_header->checkver_time);
    _Upgrade.Check(UpgradeCallback, hWnd);
    SetTimer(hWnd, IDT_TIMER_UPGRADE, 24 * 60 * 60 * 1000 /*one day*/, NULL);
}

BOOL UpgradeCallback(void *param, json_item_data_t *item)
{
    HWND hWnd = (HWND)param;

    PostMessage(hWnd, WM_NEW_VERSION, 0, NULL);
    return TRUE;
}

chkbook_arg_t* GetCheckBookArguments()
{
    static chkbook_arg_t* s_arg = NULL;
    if (!s_arg)
    {
        s_arg = new chkbook_arg_t;
    }
    return s_arg;
}

void StartCheckBookUpdate(HWND hWnd)
{
    KillTimer(hWnd, IDT_TIMER_CHECKBOOK);
    SetTimer(hWnd, IDT_TIMER_CHECKBOOK, 60 * 1000 /*one minute*/, NULL);
}

void OnCheckBookUpdateCallback(int is_update, int err, void* param)
{
    chkbook_arg_t* arg = (chkbook_arg_t*)param;
    int i;

    if (!arg)
        return;

    if (!arg->book)
        return;

    logger_printk("file=%s", Utf16ToAnsi(arg->book->GetFileName()));

    if (is_update)
    {
        for (i = 0; i < _header->item_count; i++)
        {
            item_t* item = _Cache.get_item(i);
            if (_tcscmp(item->file_name, arg->book->GetFileName()) == 0)
            {
                item->is_new = TRUE;
                Save(arg->hWnd);
                break;
            }
        }

        OnUpdateMenu(arg->hWnd);

        if (arg->book == _Book)
        {
            arg->book = NULL;
            PostMessage(arg->hWnd, WM_UPDATE_CHAPTERS, 0, NULL);
        }
    }

    if (arg->book)
    {
        if (arg->book != _Book)
            delete arg->book;
        arg->book = NULL;
    }   

    // check next book
    if (arg->hWnd)
    {
        KillTimer(arg->hWnd, IDT_TIMER_CHECKBOOK);
        SetTimer(arg->hWnd, IDT_TIMER_CHECKBOOK, 60 * 1000 /*one minute*/, NULL);
    }
}

void OnManualCheckBookUpdateCallback(int is_update, int err, void* param)
{
    chkbook_arg_t* arg = (chkbook_arg_t*)param;

    if (!arg)
        return;

    if (!arg->book)
        return;

    logger_printk("file=%s", Utf16ToAnsi(arg->book->GetFileName()));

    if (is_update)
    {
        if (arg->book == _Book)
        {
            arg->book = NULL;
            PostMessage(arg->hWnd, WM_UPDATE_CHAPTERS, 0, NULL);
        }
    }
    
    if (arg->book)
    {
        if (arg->book != _Book)
            delete arg->book;
        arg->book = NULL;
    }
}

void OnCheckBookUpdate(HWND hWnd)
{
    chkbook_arg_t* arg = GetCheckBookArguments();
    item_t* item = NULL;
    int i;
    int found = -1;

    if (!arg)
        return;

    if (arg->book)
    {
        if (arg->book != _Book)
            delete arg->book;
        arg->book = NULL;
    }   

_check_next:
    found = -1;
    // find an online book
    for (i = 0; i < _header->item_count; i++)
    {
        item = _Cache.get_item(i);
        if (0 == _tcsicmp(PathFindExtension(item->file_name), _T(".ol")))
        {
            if (arg->checked_list.find(item->file_name) != arg->checked_list.end())
            {
                continue;
            }
            found = i;
            break;
        }
    }

    if (found != -1)
    {
        item = _Cache.get_item(found);
        if (_Book && _Book->GetBookType() == book_online && _tcscmp(_Book->GetFileName(), item->file_name) == 0)
        {
            arg->hWnd = hWnd;
            arg->checked_list.insert(item->file_name);
            arg->book = (OnlineBook*)_Book;
            if (2 != ((OnlineBook*)_Book)->CheckUpdate(hWnd, OnCheckBookUpdateCallback, arg))
            {
                found = -1;
                goto _check_next;
            }
        }
        else
        {
            arg->hWnd = hWnd;
            arg->checked_list.insert(item->file_name);
            arg->book = new OnlineBook;
            arg->book->SetFileName(item->file_name);
            if (2 != arg->book->CheckUpdate(hWnd, OnCheckBookUpdateCallback, arg))
            {
                if (arg->book != _Book)
                    delete arg->book;
                arg->book = NULL;

                found = -1;
                goto _check_next;
            }
        }
    }
    else
    {
        if (arg->book)
        {
            if (arg->book != _Book)
                delete arg->book;
            arg->book = NULL;
        }
        arg->checked_list.clear();
        KillTimer(hWnd, IDT_TIMER_CHECKBOOK);
        SetTimer(hWnd, IDT_TIMER_CHECKBOOK, 60 * 60 * 1000 /*one hour*/, NULL);
    }
}
#endif

BOOL PlayLoadingImage(HWND hWnd)
{
    UINT count;
    GUID *pDimensionIDs = NULL;
    WCHAR strGuid[39];
    UINT size;
    GUID Guid = Gdiplus::FrameDimensionTime;

    if (!_loading)
    {
        // create loading data
        _loading = (loading_data_t *)malloc(sizeof(loading_data_t));
        memset(_loading, 0, sizeof(loading_data_t));

        // load image from resource
        if (!LoadResourceImage(MAKEINTRESOURCE(IDR_GIF_LOADING), _T("GIF"), &(_loading->image), &(_loading->hMemory)))
        {
            free(_loading);
            _loading = NULL;
            return FALSE;
        }

        count = _loading->image->GetFrameDimensionsCount();
        pDimensionIDs = (GUID*)malloc(sizeof(GUID)*count);
        _loading->image->GetFrameDimensionsList(pDimensionIDs, count);
        StringFromGUID2(pDimensionIDs[0], strGuid, 39);
        _loading->frameCount = _loading->image->GetFrameCount(&pDimensionIDs[0]);
        free(pDimensionIDs);
        size = _loading->image->GetPropertyItemSize(PropertyTagFrameDelay);
        _loading->item = (Gdiplus::PropertyItem*)malloc(size);
        _loading->image->GetPropertyItem(PropertyTagFrameDelay, size, _loading->item);
    }

    _loading->enable = TRUE;
    _loading->frameIndex = 0;
    Guid = Gdiplus::FrameDimensionTime;
    _loading->image->SelectActiveFrame(&Guid, _loading->frameIndex);

    SetTimer(hWnd, IDT_TIMER_LOADING, ((UINT*)(_loading->item[0].value))[_loading->frameIndex] * 10, NULL);
    _loading->frameIndex = (++ _loading->frameIndex) % _loading->frameCount;
    Invalidate(hWnd, TRUE, FALSE);
    return TRUE;
}

BOOL StopLoadingImage(HWND hWnd)
{
    if (!_loading)
        return FALSE;
    _loading->enable = FALSE;
    KillTimer(hWnd, IDT_TIMER_LOADING);
    Invalidate(hWnd, TRUE, FALSE);
    return TRUE;
}

ULONGLONG GetDllVersion(LPCTSTR lpszDllName)
{
    ULONGLONG ullVersion = 0;
    HINSTANCE hinstDll;
    hinstDll = LoadLibrary(lpszDllName);
    if(hinstDll)
    {
        DLLGETVERSIONPROC pDllGetVersion;
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, "DllGetVersion");
        if(pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            HRESULT hr;
            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);
            hr = (*pDllGetVersion)(&dvi);
            if(SUCCEEDED(hr))
                ullVersion = MAKEDLLVERULL(dvi.dwMajorVersion, dvi.dwMinorVersion,0,0);
        }
        FreeLibrary(hinstDll);
    }
    return ullVersion;
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
    TCHAR szClassName[MAX_LOADSTRING] = {0};
    HWND hForeWnd = NULL;
    DWORD dwForeID;
    DWORD dwCurID;
    HWND *p_hWnd = (HWND*)lParam;

    GetClassName(hWnd, szClassName, MAX_LOADSTRING);
    if (_tcscmp(szClassName, szWindowClass) == 0)
    {
        hForeWnd = GetForegroundWindow();
        dwCurID = GetCurrentThreadId();
        dwForeID = GetWindowThreadProcessId(hForeWnd, NULL);
        AttachThreadInput(dwCurID, dwForeID, TRUE);
        ShowWindow(hWnd, SW_SHOWNORMAL);
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetForegroundWindow(hWnd);
        AttachThreadInput(dwCurID, dwForeID, FALSE);
        *p_hWnd = hWnd;
        return FALSE;
    }
    return TRUE;
}

void ShowInTaskbar(HWND hWnd, BOOL bShow)
{
    HRESULT hr;
    ITaskbarList* pTaskbarList;
    hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_ITaskbarList, (void**)&pTaskbarList );

    if(SUCCEEDED(hr))
    {
        pTaskbarList->HrInit();
        if(bShow)
            pTaskbarList->AddTab(hWnd);
        else
            pTaskbarList->DeleteTab(hWnd);

        pTaskbarList->Release();
    }
}

void ShowSysTray(HWND hWnd, BOOL bShow)
{
    if (bShow)
    {
        if (!_nid.hWnd)
        {
            // create a systray
            ULONGLONG ullVersion = GetDllVersion(_T("Shell32.dll"));
            ZeroMemory(&_nid, sizeof(NOTIFYICONDATA));
            if(ullVersion >= MAKEDLLVERULL(6,0,0,0))
                _nid.cbSize = sizeof(NOTIFYICONDATA);
            else if(ullVersion >= MAKEDLLVERULL(5,0,0,0))
                _nid.cbSize = NOTIFYICONDATA_V2_SIZE;
            else
                _nid.cbSize = NOTIFYICONDATA_V1_SIZE;
            _nid.hWnd = hWnd;
            _nid.uID = IDI_SYSTRAY;
            _nid.uCallbackMessage = WM_SYSTRAY;
            _nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_BOOK));
            _tcscpy(_nid.szTip, szTitle);
        }
        if (_nid.uFlags == 0)
        {
            _nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
            Shell_NotifyIcon(NIM_ADD, &_nid);
        }
    }
    else
    {
        if (_nid.uFlags)
        {
            _nid.uFlags = 0;
            Shell_NotifyIcon(NIM_DELETE, &_nid);
        }
    }
}

HBITMAP CreateAlphaBGColorBitmap(HWND hWnd, COLORREF inColour, int width, int height, BYTE alpha)
{
    // Create DC and select font into it
    HDC hColorDC = CreateCompatibleDC(NULL);
    HBITMAP hDIB = NULL;
    BITMAPINFOHEADER BMIH;
    void *pvBits = NULL;
    HBITMAP hOldBMP = NULL;
    BYTE* DataPtr = NULL;
    BYTE FillR,FillG,FillB;
    int x,y;

    // Specify DIB setup
    memset(&BMIH, 0x0, sizeof(BITMAPINFOHEADER));
    BMIH.biSize = sizeof(BMIH);
    BMIH.biWidth = width;
    BMIH.biHeight = height;
    BMIH.biPlanes = 1;
    BMIH.biBitCount = 32;
    BMIH.biCompression = BI_RGB;

    // Create and select DIB into DC
    hDIB = CreateDIBSection(hColorDC, (LPBITMAPINFO)&BMIH, 0, (LPVOID*)&pvBits, NULL, 0); 
    hOldBMP = (HBITMAP)SelectObject(hColorDC, hDIB);
    if (hOldBMP)
    {
        DataPtr = (BYTE*)pvBits;
        FillR = GetRValue(inColour);
        FillG = GetGValue(inColour);
        FillB = GetBValue(inColour);
        for (y = 0; y < BMIH.biHeight; y++)
        { 
            for (x = 0; x < BMIH.biWidth; x++)
            {
                *DataPtr++ = (FillB * alpha) >> 8; 
                *DataPtr++ = (FillG * alpha) >> 8; 
                *DataPtr++ = (FillR * alpha) >> 8;
                *DataPtr++ = alpha;
            }
        }

        // De-select bitmap
        SelectObject(hColorDC, hOldBMP);
    }

    // destroy temp DC
    DeleteDC(hColorDC);

    // Return DIBSection 
    return hDIB;
}

void SetTreeviewFont()
{
    static HFONT s_hFont = NULL;

    if (s_hFont)
    {
        DeleteObject(s_hFont);
        s_hFont = NULL;
    }

    if (_header->meun_font_follow)
    {
        s_hFont = CreateFontIndirect(&_header->font);
        int height = abs((int)(_header->font.lfHeight * 1.5f));
        SendMessage(_hTreeView, WM_SETFONT, (WPARAM)s_hFont, NULL);
        SendMessage(_hTreeView, TVM_SETITEMHEIGHT, height, NULL);
        SendMessage(_hTreeMark, WM_SETFONT, (WPARAM)s_hFont, NULL);
        SendMessage(_hTreeMark, TVM_SETITEMHEIGHT, height, NULL);
    }
    else
    {
        NONCLIENTMETRICS theMetrics = {0};
        theMetrics.cbSize = sizeof(NONCLIENTMETRICS);
        if (Is_WinXP_SP2_or_Later())
            theMetrics.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(theMetrics.iPaddedBorderWidth);
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(NONCLIENTMETRICS), (PVOID) &theMetrics,0);
        theMetrics.iMenuHeight = abs((int)(theMetrics.iMenuHeight * 1.25f)); // 1.25 line height
        if (theMetrics.iMenuHeight == 0)
        {
            theMetrics.iMenuHeight = 20;
        }
        s_hFont = CreateFontIndirect(&(theMetrics.lfMenuFont));
        SendMessage(_hTreeView, WM_SETFONT, (WPARAM)s_hFont, NULL);
        SendMessage(_hTreeView, TVM_SETITEMHEIGHT, theMetrics.iMenuHeight, NULL);
        SendMessage(_hTreeMark, WM_SETFONT, (WPARAM)s_hFont, NULL);
        SendMessage(_hTreeMark, TVM_SETITEMHEIGHT, theMetrics.iMenuHeight, NULL);
    }
}

BOOL LoadResourceImage(LPCWSTR lpName, LPCWSTR lpType, Gdiplus::Bitmap **image, HGLOBAL *hMemory)
{
    HRSRC hResource;
    HGLOBAL hResLoad;
    const void* pResourceData = NULL;
    IStream* pStream = NULL;
    DWORD imageSize;
    void* buffer = NULL;

    hResource = ::FindResource(hInst, lpName, lpType);
    if (!hResource)
        return FALSE;

    hResLoad = ::LoadResource(hInst, hResource);
    if (!hResLoad)
        return FALSE;

    pResourceData = ::LockResource(hResLoad);
    if (!pResourceData)
    {
        ::FreeResource(hResLoad);
        return FALSE;
    }

    imageSize = ::SizeofResource(hInst, hResource);
    if (0 == imageSize)
    {
        ::FreeResource(hResLoad);
        return FALSE;
    }

    *hMemory = ::GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (!(*hMemory))
    {
        ::FreeResource(hResLoad);
        return FALSE;
    }

    buffer = ::GlobalLock(*hMemory);
    if (!buffer)
    {
        ::FreeResource(hResLoad);
        ::GlobalFree(*hMemory);
        return FALSE;
    }

    CopyMemory(buffer, pResourceData, imageSize);

    if (::CreateStreamOnHGlobal(*hMemory, FALSE, &pStream) == S_OK)
    {
        *image = Gdiplus::Bitmap::FromStream(pStream);
        pStream->Release();
    }
    ::FreeResource(hResLoad);
    ::GlobalUnlock(*hMemory);
    //::GlobalFree(hMemory);

    if ((*image) && Gdiplus::Ok == (*image)->GetLastStatus())
    {
        return TRUE;
    }

    if (*image)
        delete (*image);
    *image = NULL;
    ::GlobalFree(*hMemory);
    *hMemory = NULL;
    return FALSE;
}

book_source_t* FindBookSource(const char* host)
{
    int i = 0;

    if (!host)
        return NULL;
    for (i = 0; i < _header->book_source_count; i++)
    {
        if (0 == strcmp(_header->book_sources[i].host, host))
            return &_header->book_sources[i];
    }
    return NULL;
}

void SetGlobalKey(HWND hWnd)
{
#if ENABLE_GLOBAL_KEY
    if (_header->global_key)
    {
        if (!_hKeyboardHook)
        {
            _hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInst, NULL);
        }
    }
    else
    {
        if (_hKeyboardHook)
        {
            UnhookWindowsHookEx(_hKeyboardHook);
            _hKeyboardHook = NULL;
        }
    }
#endif
}

HHOOK hMsgBoxHook = NULL;
LRESULT CALLBACK MsgBoxHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    HWND hParent;
    RECT rc;
    int w, h, x, y;
    LPCBT_CREATEWND cbt;
    if (nCode == HCBT_CREATEWND)
    {
        cbt = ((LPCBT_CREATEWND)lParam);

        if (cbt->lpcs->lpszClass == (LPWSTR)(ATOM)32770)  // #32770 = dialog box class
        {
            hParent = cbt->lpcs->hwndParent;
            if (hParent)
            {
                GetWindowRect(hParent, &rc);
                w = rc.right - rc.left;
                h = rc.bottom - rc.top;
                x = rc.left + (w - cbt->lpcs->cx) / 2;
                y = rc.top + (h - cbt->lpcs->cy) / 2;
                cbt->lpcs->x = x;
                cbt->lpcs->y = y;
            }
        }
    }

    return CallNextHookEx(hMsgBoxHook, nCode, wParam, lParam);
}

int MessageBox_(HWND hWnd, UINT textId, UINT captionId, UINT uType)
{
    TCHAR szText[MAX_LOADSTRING] = { 0 };
    TCHAR szCaption[MAX_LOADSTRING] = { 0 };
    int res;

    hMsgBoxHook = SetWindowsHookEx(WH_CBT, MsgBoxHookProc, hInst, GetCurrentThreadId());
    LoadString(hInst, textId, szText, MAX_LOADSTRING);
    LoadString(hInst, captionId, szCaption, MAX_LOADSTRING);
    res = MessageBoxEx(hWnd, szText, szCaption, uType, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));

    if (hMsgBoxHook)
        UnhookWindowsHookEx(hMsgBoxHook);
    return res;
}

int MessageBoxFmt_(HWND hWnd, UINT captionId, UINT uType, UINT formatId, ...)
{
    TCHAR szText[MAX_LOADSTRING] = { 0 };
    TCHAR szCaption[MAX_LOADSTRING] = { 0 };
    TCHAR szFormat[MAX_LOADSTRING] = { 0 };
    int res;
    va_list args;

    hMsgBoxHook = SetWindowsHookEx(WH_CBT, MsgBoxHookProc, hInst, GetCurrentThreadId());
    LoadString(hInst, formatId, szFormat, MAX_LOADSTRING);
    LoadString(hInst, captionId, szCaption, MAX_LOADSTRING);

    va_start(args, formatId);
    _vsntprintf(szText, MAX_LOADSTRING, szFormat, args);
    va_end(args);
    
    res = MessageBoxEx(hWnd, szText, szCaption, uType, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));

    if (hMsgBoxHook)
        UnhookWindowsHookEx(hMsgBoxHook);
    return res;
}
