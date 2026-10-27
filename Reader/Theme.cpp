#include "Theme.h"
#include "ReaderInteraction.h"
#include "Scrollbar.h"
#include "DrawReaderIcon.h"
#include <algorithm>
#include "DPIAwareness.h"
#include <uxtheme.h>
#include <oleacc.h>
#include <memory>
#include <windowsx.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cwctype>

#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")

namespace {
const UINT_PTR subclassId = 0x5254484d;
ReaderPalette palette = MakeReaderPalette(0);
ReaderColors reading = { 0xffffff, 0, 0, false };
HBRUSH windowBrush = NULL, surfaceBrush = NULL, readingBrush = NULL;
std::set<HWND> roots;
struct WindowData { bool root, hot; std::wstring type; WindowData(bool r, const wchar_t* c) : root(r), hot(false), type(c) {} };
struct MenuData { MSAAMENUINFO accessible; HMENU menu; UINT position, type; ULONG_PTR previous; std::wstring text; };
std::map<ULONG_PTR, std::unique_ptr<MenuData>> menus;
bool ClassIs(const WindowData* d, const wchar_t* type) { return !_wcsicmp(d->type.c_str(), type); }
void Attach(HWND, bool);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

void Fill(HDC dc, const RECT& rect, COLORREF color)
{ HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush); }
void Frame(HDC dc, RECT rect, COLORREF color)
{ HBRUSH brush = CreateSolidBrush(color); FrameRect(dc, &rect, brush); DeleteObject(brush); }
std::wstring Text(HWND window)
{ int n = GetWindowTextLength(window); std::vector<wchar_t> s(n + 1, 0); GetWindowText(window, s.data(), n + 1); return s.data(); }

void PaintButton(HWND window, HDC dc, WindowData* data)
{
    RECT rect; GetClientRect(window, &rect);
    bool sidebar = GetProp(window,L"ReaderSidebarItem") != NULL;
    ReaderIcon icon = (ReaderIcon)(INT_PTR)GetProp(window,L"ReaderIcon");
    bool flatIcon = GetProp(window,L"ReaderFlatIcon") != NULL;
    if (sidebar || flatIcon)
    {
        bool enabled=IsWindowEnabled(window)!=FALSE, focus=GetFocus()==window;
        LRESULT state=SendMessage(window,BM_GETSTATE,0,0);
        bool selected=sidebar && (SendMessage(window,BM_GETCHECK,0,0)==BST_CHECKED);
        auto colors=ReaderFlatControlColors(ThemeControlPalette(window),enabled,data->hot,(state&BST_PUSHED)!=0,selected);
        Fill(dc,rect,colors.background); COLORREF color=colors.ink;
        int dpi=GetWindowDpiCompat(window), size=MulDiv(sidebar?20:18,dpi,96);
        bool compact=GetProp(window,L"ReaderCompact")!=NULL;
        int left=sidebar&&!compact?ReaderSidebarPadding(dpi):(rect.right-size)/2;
        RECT bounds={left,(rect.bottom-size)/2,left+size,(rect.bottom+size)/2}; DrawReaderIcon(dc,icon,bounds,color);
        if(sidebar && !compact)
        {
            RECT label=rect; label.left=MulDiv(46,dpi,96); label.right-=MulDiv(10,dpi,96);
            HFONT font=(HFONT)SendMessage(window,WM_GETFONT,0,0); auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color); std::wstring text=Text(window);
            DrawText(dc,text.c_str(),-1,&label,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS); SelectObject(dc,previous);
        }
        if(focus) { RECT cue=rect; InflateRect(&cue,-MulDiv(3,dpi,96),-MulDiv(3,dpi,96)); DrawFocusRect(dc,&cue); }
        return;
    }
    LONG style = GetWindowLong(window, GWL_STYLE);
    int kind = style & BS_TYPEMASK;
    bool enabled = IsWindowEnabled(window) != FALSE;
    LRESULT state = SendMessage(window, BM_GETSTATE, 0, 0);
    bool down = (state & BST_PUSHED) != 0, focused = GetFocus() == window;
    bool check = kind == BS_CHECKBOX || kind == BS_AUTOCHECKBOX || kind == BS_3STATE || kind == BS_AUTO3STATE;
    bool radio = kind == BS_RADIOBUTTON || kind == BS_AUTORADIOBUTTON;
    bool group = kind == BS_GROUPBOX;
    COLORREF background = group || check || radio ? palette.window : down ? palette.pressed : data->hot ? palette.hover : palette.raised;
    Fill(dc, rect, background);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, enabled ? palette.text : palette.disabled);
    HFONT font = (HFONT)SendMessage(window, WM_GETFONT, 0, 0);
    HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT label = rect;
    int unit = MulDiv(14, GetWindowDpiCompat(window), 96);
    if (group)
    {
        RECT border = rect; border.top += unit / 2; Frame(dc, border, palette.border);
        label.left += unit / 2; label.right -= unit / 2;
        SIZE size = {}; std::wstring text = Text(window); GetTextExtentPoint32(dc, text.c_str(), (int)text.size(), &size);
        RECT patch = { label.left, 0, label.left + size.cx + 6, size.cy }; Fill(dc, patch, palette.window);
        DrawText(dc, text.c_str(), -1, &label, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    else
    {
        if (check || radio)
        {
            RECT box = { 1, (rect.bottom - unit) / 2, 1 + unit, (rect.bottom + unit) / 2 };
            HBRUSH brush = CreateSolidBrush(palette.surface); HPEN pen = CreatePen(PS_SOLID, 1, focused ? palette.accent : palette.border);
            HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
            if (radio) Ellipse(dc, box.left, box.top, box.right, box.bottom); else Rectangle(dc, box.left, box.top, box.right, box.bottom);
            SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
            if (state & (BST_CHECKED | BST_INDETERMINATE))
            {
                InflateRect(&box, -3, -3); Fill(dc, box, enabled ? palette.accent : palette.disabled);
            }
            label.left += unit + 6;
        }
        else { Frame(dc, rect, focused || kind == BS_DEFPUSHBUTTON ? palette.accent : palette.border); InflateRect(&label, -3, -2); }
        std::wstring text = Text(window);
        DrawText(dc, text.c_str(), -1, &label, (style & BS_MULTILINE ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER) | (check || radio ? DT_LEFT : DT_CENTER));
        if (focused) { RECT focus = label; InflateRect(&focus, -1, -1); DrawFocusRect(dc, &focus); }
    }
    SelectObject(dc, previous);
}

void PaintCombo(HWND window, HDC dc)
{
    RECT rc; GetClientRect(window, &rc); Fill(dc, rc, palette.surface);
    Frame(dc, rc, GetFocus() == window ? palette.accent : palette.border);
    RECT arrow = rc; arrow.left = (std::max)(0L, rc.right - MulDiv(20, GetWindowDpiCompat(window), 96));
    Fill(dc, arrow, palette.raised);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(window) ? palette.text : palette.disabled);
    HGDIOBJ previous = SelectObject(dc, (HFONT)SendMessage(window, WM_GETFONT, 0, 0));
    DrawText(dc, L"▾", 1, &arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((GetWindowLong(window, GWL_STYLE) & 3) == CBS_DROPDOWNLIST)
    {
        std::wstring value = Text(window); RECT text = rc; text.left += 5; text.right = arrow.left - 3;
        DrawText(dc, value.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (GetFocus() == window) { InflateRect(&text, -1, -2); DrawFocusRect(dc, &text); }
    }
    SelectObject(dc, previous);
}

void PaintStatus(HWND window, HDC dc)
{
    RECT rc; GetClientRect(window, &rc); Fill(dc, rc, palette.window);
    int n = LOWORD(SendMessage(window, SB_GETTEXTLENGTH, 0, 0));
    std::vector<wchar_t> text(n + 1, 0); SendMessage(window, SB_GETTEXT, 0, (LPARAM)text.data());
    HGDIOBJ previous = SelectObject(dc, (HFONT)SendMessage(window, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, palette.text); rc.left += 5;
    DrawText(dc, text.data(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
}

// The reading trees are flat native lists. Custom-draw only their rows so the
// current chapter uses the same neutral selection as the library, independent
// of Windows' system highlight color. Hit testing, scrolling and accessibility
// continue to use the original TreeView items and font metrics.
LRESULT DrawReadingRow(HWND tree,NMTVCUSTOMDRAW* draw)
{
    HTREEITEM handle=(HTREEITEM)draw->nmcd.dwItemSpec;
    bool current=(HTREEITEM)GetProp(tree,L"ReaderCurrentChapter")==handle;
    bool selected=(draw->nmcd.uItemState&CDIS_SELECTED)!=0;
    bool active=GetProp(tree,L"ReaderCurrentChapter")?current:selected;
    RECT row=draw->nmcd.rc,client; GetClientRect(tree,&client); row.left=0;row.right=client.right;
    int saved=SaveDC(draw->nmcd.hdc); IntersectClipRect(draw->nmcd.hdc,row.left,row.top,row.right,row.bottom);
    COLORREF ink=!IsWindowEnabled(tree)?palette.disabled:active?palette.selectedText:palette.text;
    Fill(draw->nmcd.hdc,row,active?palette.selected:selected?palette.hover:palette.surface);
    std::vector<wchar_t> text(512,0); TVITEM item={};item.hItem=handle;item.mask=TVIF_TEXT;
    for(;;)
    {
        item.pszText=text.data(); item.cchTextMax=(int)text.size();
        if(!TreeView_GetItem(tree,&item) || wcslen(text.data())+1<text.size() || text.size()>=65536) break;
        text.resize(text.size()*2,0);
    }
    RECT label=row; TreeView_GetItemRect(tree,handle,&label,TRUE);
    label.top=row.top;label.bottom=row.bottom;
    HFONT font=(HFONT)SendMessage(tree,WM_GETFONT,0,0); if(font) SelectObject(draw->nmcd.hdc,font);
    SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,ink);
    DrawText(draw->nmcd.hdc,text.data(),-1,&label,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    if(GetFocus()==tree && selected) { RECT cue=row;InflateRect(&cue,-3,-3);DrawFocusRect(draw->nmcd.hdc,&cue); }
    int inset=MulDiv(12,GetWindowDpiCompat(tree),96);row.left=inset;row.right-=inset;row.top=row.bottom-1;
    Fill(draw->nmcd.hdc,row,palette.border);RestoreDC(draw->nmcd.hdc,saved);
    return CDRF_SKIPDEFAULT;
}

LRESULT DrawPaddedCell(HWND list,NMLVCUSTOMDRAW* draw)
{
    const int row=(int)draw->nmcd.dwItemSpec,column=draw->iSubItem;
    RECT cell={};ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&cell);
    if(column==0) {RECT heading;HWND header=ListView_GetHeader(list);if(Header_GetItemRect(header,0,&heading)) {MapWindowPoints(header,list,(POINT*)&heading,2);cell.left=heading.left;cell.right=heading.right;}}
    const bool selected=(ListView_GetItemState(list,row,LVIS_SELECTED)&LVIS_SELECTED)!=0;
    const bool library=GetProp(list,L"ReaderLibraryList")!=nullptr;
    int saved=SaveDC(draw->nmcd.hdc);IntersectClipRect(draw->nmcd.hdc,cell.left,cell.top,cell.right,cell.bottom);
    Fill(draw->nmcd.hdc,cell,selected?palette.selected:palette.surface);
    HFONT font=library&&column==0?(HFONT)GetProp(list,L"ReaderTitleFont"):(HFONT)SendMessage(list,WM_GETFONT,0,0);
    if(font) SelectObject(draw->nmcd.hdc,font);
    std::vector<wchar_t> text(512,0);
    for(;;)
    {
        LVITEM item={};item.iSubItem=column;item.pszText=text.data();item.cchTextMax=(int)text.size();
        int size=(int)SendMessage(list,LVM_GETITEMTEXT,row,(LPARAM)&item);
        if(size+1<(int)text.size()||text.size()>=65536) break;text.resize(text.size()*2,0);
    }
    RECT label=cell;int padding=ReaderItemInset(cell.right-cell.left,GetWindowDpiCompat(list));label.left+=padding;label.right-=padding;
    SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,!IsWindowEnabled(list)?palette.disabled:selected?palette.selectedText:library&&column>0?palette.muted:palette.text);
    DrawText(draw->nmcd.hdc,text.data(),-1,&label,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
    if(GetFocus()==list && (ListView_GetItemState(list,row,LVIS_FOCUSED)&LVIS_FOCUSED)) {RECT cue=cell;InflateRect(&cue,-1,-1);DrawFocusRect(draw->nmcd.hdc,&cue);}
    RestoreDC(draw->nmcd.hdc,saved);return CDRF_SKIPDEFAULT;
}

void RestoreMenu(HMENU menu)
{
    for (auto it = menus.begin(); it != menus.end();)
    {
        if (it->second->menu != menu) { ++it; continue; }
        MENUITEMINFO item = {}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA | MIIM_FTYPE;
        if (IsMenu(menu) && GetMenuItemInfo(menu, it->second->position, TRUE, &item) && item.dwItemData == it->first)
        { item.fType = it->second->type; item.dwItemData = it->second->previous; SetMenuItemInfo(menu, it->second->position, TRUE, &item); }
        it = menus.erase(it);
    }
}
void PrepareMenu(HMENU menu)
{
    if (!menu) return;
    for (auto it = menus.begin(); it != menus.end();)
    {
        HMENU owner = it->second->menu; int position = -1;
        if (IsMenu(owner)) for (int i = 0; i < GetMenuItemCount(owner); ++i)
        {
            MENUITEMINFO item = {}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA;
            if (GetMenuItemInfo(owner, i, TRUE, &item) && item.dwItemData == it->first) { position = i; break; }
        }
        if (position < 0) it = menus.erase(it);
        else { it->second->position = position; ++it; }
    }
    MENUINFO background = {}; background.cbSize = sizeof(background); background.fMask = MIM_BACKGROUND; background.hbrBack = windowBrush;
    SetMenuInfo(menu, &background);
    for (int i = 0; i < GetMenuItemCount(menu); ++i)
    {
        MENUITEMINFO item = {}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA | MIIM_FTYPE | MIIM_SUBMENU;
        if (!GetMenuItemInfo(menu, i, TRUE, &item)) continue;
        if (menus.find(item.dwItemData) == menus.end() || !(item.fType & MFT_OWNERDRAW))
        {
            int n = GetMenuString(menu, i, NULL, 0, MF_BYPOSITION);
            std::vector<wchar_t> text(n + 1, 0); GetMenuString(menu, i, text.data(), n + 1, MF_BYPOSITION);
            auto previous = menus.find(item.dwItemData);
            if (previous != menus.end()) { item.dwItemData = previous->second->previous; menus.erase(previous); }
            std::unique_ptr<MenuData> data(new MenuData{ {}, menu, (UINT)i, item.fType, item.dwItemData, text.data() });
            data->accessible = { MSAA_MENU_SIG, (DWORD)data->text.size(), const_cast<wchar_t*>(data->text.c_str()) };
            ULONG_PTR id = (ULONG_PTR)&data->accessible; menus[id] = std::move(data);
            item.fMask = MIIM_DATA | MIIM_FTYPE; item.dwItemData = id; item.fType |= MFT_OWNERDRAW;
            SetMenuItemInfo(menu, i, TRUE, &item);
        }
        if (item.hSubMenu) PrepareMenu(item.hSubMenu);
    }
}

bool DrawMenu(HWND window, UINT message, LPARAM parameter, LRESULT& result)
{
    ULONG_PTR id = message == WM_DRAWITEM ? ((DRAWITEMSTRUCT*)parameter)->itemData : ((MEASUREITEMSTRUCT*)parameter)->itemData;
    auto found = menus.find(id); if (found == menus.end()) return false;
    const MenuData& data = *found->second;
    int dpi = GetWindowDpiCompat(window);
    NONCLIENTMETRICS metrics = {}; metrics.cbSize = sizeof(metrics); SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    metrics.lfMenuFont.lfHeight = -MulDiv(9, dpi, 72); HFONT font = CreateFontIndirect(&metrics.lfMenuFont);
    if (message == WM_MEASUREITEM)
    {
        auto item = (MEASUREITEMSTRUCT*)parameter; HDC dc = GetDC(window); auto previous = SelectObject(dc, font);
        SIZE size = {}; GetTextExtentPoint32(dc, data.text.c_str(), (int)data.text.size(), &size);
        item->itemWidth = size.cx + MulDiv(42, dpi, 96); item->itemHeight = MulDiv(data.type & MFT_SEPARATOR ? 7 : 25, dpi, 96);
        SelectObject(dc, previous); ReleaseDC(window, dc);
    }
    else
    {
        auto item = (DRAWITEMSTRUCT*)parameter; bool selected = (item->itemState & ODS_SELECTED) != 0;
        Fill(item->hDC, item->rcItem, selected ? palette.selected : palette.window);
        RECT text = item->rcItem; text.left += MulDiv(22, dpi, 96); text.right -= MulDiv(15, dpi, 96);
        if (data.type & MFT_SEPARATOR) { RECT line = text; line.top = (text.top + text.bottom) / 2; line.bottom = line.top + 1; Fill(item->hDC, line, palette.border); }
        else
        {
            auto previous = SelectObject(item->hDC, font); SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, item->itemState & (ODS_DISABLED | ODS_GRAYED) ? palette.disabled : selected ? palette.selectedText : palette.text);
            std::wstring label = data.text; size_t tab = label.find(L'\t');
            DrawText(item->hDC, label.substr(0, tab).c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | (item->itemState & ODS_NOACCEL && !GetProp(window,L"ReaderMenuKeyboard") ? DT_HIDEPREFIX : 0));
            if (tab != std::wstring::npos) DrawText(item->hDC, label.substr(tab + 1).c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
            MENUITEMINFO menuItem = {}; menuItem.cbSize = sizeof(menuItem); menuItem.fMask = MIIM_SUBMENU | MIIM_BITMAP;
            GetMenuItemInfo(data.menu, data.position, TRUE, &menuItem);
            if (item->itemState & ODS_CHECKED) { RECT check = item->rcItem; check.right = text.left; DrawText(item->hDC, L"✓", 1, &check, DT_CENTER | DT_SINGLELINE | DT_VCENTER); }
            else if (menuItem.hbmpItem)
            {
                BITMAP bitmap = {};
                if (GetObject(menuItem.hbmpItem, sizeof(bitmap), &bitmap)) DrawState(item->hDC, NULL, NULL, (LPARAM)menuItem.hbmpItem, 0,
                    item->rcItem.left + MulDiv(3, dpi, 96), item->rcItem.top + ((item->rcItem.bottom - item->rcItem.top - bitmap.bmHeight) / 2),
                    bitmap.bmWidth, bitmap.bmHeight, DST_BITMAP | (item->itemState & (ODS_DISABLED | ODS_GRAYED) ? DSS_DISABLED : DSS_NORMAL));
            }
            if (menuItem.hSubMenu) { RECT arrow = item->rcItem; arrow.left = text.right; DrawText(item->hDC, L"›", 1, &arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
            SelectObject(item->hDC, previous);
        }
    }
    DeleteObject(font); result = TRUE; return true;
}

HMENU WindowMenu(HWND window)
{ HMENU menu=GetMenu(window); return menu?menu:(HMENU)GetProp(window,L"ReaderMainMenu"); }
void PrepareWindowMenu(HWND window)
{
    HMENU menu=WindowMenu(window);
    if(GetMenu(window)) PrepareMenu(menu);
    else if(menu) for(int i=0;i<GetMenuItemCount(menu);++i) PrepareMenu(GetSubMenu(menu,i));
}

void Apply(HWND window, WindowData* data)
{
    if (ClassIs(data, WC_TREEVIEW))
    {
        SetWindowTheme(window, L"", L"");
        TreeView_SetBkColor(window, palette.surface); TreeView_SetTextColor(window, palette.text);
        TreeView_SetLineColor(window, palette.border);
        HWND tip = TreeView_GetToolTips(window);
        if (tip) { Attach(tip, false); SendMessage(tip, TTM_SETMAXTIPWIDTH, 0, MulDiv(600, GetWindowDpiCompat(window), 96)); }
    }
    if (ClassIs(data, WC_LISTVIEW))
    {
        SetWindowTheme(window, L"", L"");
        ListView_SetBkColor(window, palette.surface); ListView_SetTextBkColor(window, palette.surface); ListView_SetTextColor(window, palette.text);
        HWND header = ListView_GetHeader(window); if (header) Attach(header, false);
        HWND tip = ListView_GetToolTips(window); if (tip) Attach(tip, false);
    }
    if (ClassIs(data, WC_COMBOBOX))
    {
        SetWindowLongPtr(window, GWL_STYLE, GetWindowLongPtr(window, GWL_STYLE) | WS_CLIPCHILDREN);
        COMBOBOXINFO info = {}; info.cbSize = sizeof(info);
        if (GetComboBoxInfo(window, &info) && info.hwndList) { SetWindowTheme(info.hwndList, L"", L""); Attach(info.hwndList, false); }
    }
    if (ClassIs(data, TOOLTIPS_CLASS)) { SetWindowTheme(window, L"", L""); SendMessage(window, TTM_SETTIPBKCOLOR, palette.raised, 0); SendMessage(window, TTM_SETTIPTEXTCOLOR, palette.text, 0); }
    if (ClassIs(data, WC_BUTTON) || ClassIs(data, WC_COMBOBOX) || ClassIs(data, STATUSCLASSNAME) || ClassIs(data, WC_HEADER) || ClassIs(data, PROGRESS_CLASS)) SetWindowTheme(window, L"", L"");
    if (ClassIs(data, PROGRESS_CLASS)) { SendMessage(window, PBM_SETBKCOLOR, 0, palette.surface); SendMessage(window, PBM_SETBARCOLOR, 0, palette.accent); }
    if (data->root) PrepareWindowMenu(window);
    InvalidateRect(window, NULL, FALSE);
}
BOOL CALLBACK AttachChild(HWND window, LPARAM) { Attach(window, false); return TRUE; }
void Attach(HWND window, bool root)
{
    if (!window || GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return;
    DWORD_PTR reference = 0;
    if (GetWindowSubclass(window, WindowProc, subclassId, &reference)) { Apply(window, (WindowData*)reference); ReaderScrollbarsAttach(window); return; }
    wchar_t type[64] = {}; GetClassName(window, type, 64);
    WindowData* data = new WindowData(root, type);
    if (!SetWindowSubclass(window, WindowProc, subclassId, (DWORD_PTR)data)) { delete data; return; }
    SetWindowLongPtr(window, GWL_STYLE, GetWindowLongPtr(window, GWL_STYLE) | WS_CLIPSIBLINGS);
    if (root) { roots.insert(window); SetWindowLongPtr(window, GWL_STYLE, GetWindowLongPtr(window, GWL_STYLE) | WS_CLIPCHILDREN); }
    Apply(window, data);
    ReaderScrollbarsAttach(window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR reference)
{
    WindowData* data = (WindowData*)reference;
    if(data->root && (message==WM_ACTIVATEAPP || message==WM_CANCELMODE || message==WM_SETTINGCHANGE))
        ReaderScrollbarsRefresh(window,message==WM_CANCELMODE || (message==WM_ACTIVATEAPP&&!wParam));
    if (message == WM_NCDESTROY)
    {
        roots.erase(window); LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        RemoveWindowSubclass(window, WindowProc, subclassId); delete data; return result;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN)
    {
        HDC dc = (HDC)wParam; HWND child = (HWND)lParam;
        if (GetProp(child, L"ReaderColorPreview")) return DefSubclassProc(window, message, wParam, lParam);
        bool editor = GetProp(child, L"ReaderReadingEditor") != NULL;
        bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX;
        SetBkMode(dc, TRANSPARENT); SetBkColor(dc, editor ? reading.background : field ? palette.surface : palette.window);
        SetTextColor(dc, !IsWindowEnabled(child) ? palette.disabled : editor ? reading.text : GetProp(child,L"ReaderMutedText") ? palette.muted : palette.text);
        return (LRESULT)(editor ? readingBrush : field ? surfaceBrush : windowBrush);
    }
    bool background = data->root && !GetProp(window, L"ReaderOwnBackground");
    if (message == WM_ERASEBKGND && background) { RECT rc; GetClientRect(window, &rc); FillRect((HDC)wParam, &rc, windowBrush); return TRUE; }
    if (message == WM_PAINT && background)
    {
        RECT dirty;
        if (GetUpdateRect(window, &dirty, FALSE))
        {
            // Fill before the native/dialog paint without consuming its update region.
            // WS_CLIPCHILDREN leaves every child responsible for its own surface.
            HDC dc = GetDC(window); FillRect(dc, &dirty, windowBrush); ReleaseDC(window, dc);
        }
    }
    if (message == WM_PARENTNOTIFY && LOWORD(wParam) == WM_CREATE) Attach((HWND)lParam, false);
    if (message == WM_INITMENU || message == WM_INITMENUPOPUP)
    {
        if ((HMENU)wParam == (HMENU)GetProp(window,L"ReaderMainMenu")) PrepareWindowMenu(window);
        else PrepareMenu((HMENU)wParam);
    }
    if (message == WM_MENUCHAR)
    {
        HMENU menu = (HMENU)lParam; int match = -1, count = 0;
        wchar_t key = (wchar_t)towupper(LOWORD(wParam));
        for (int i = 0; i < GetMenuItemCount(menu); ++i)
        {
            MENUITEMINFO item = {}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA | MIIM_STATE;
            if (!GetMenuItemInfo(menu, i, TRUE, &item) || (item.fState & (MFS_DISABLED | MFS_GRAYED))) continue;
            auto found = menus.find(item.dwItemData); if (found == menus.end()) continue;
            const std::wstring& text = found->second->text;
            for (size_t j = 0; j + 1 < text.size(); ++j) if (text[j] == L'&')
            {
                if (text[j + 1] == L'&') { ++j; continue; }
                if (towupper(text[j + 1]) == key) { if (match < 0) match = i; ++count; }
                break;
            }
        }
        if (match >= 0) return MAKELRESULT(match, count == 1 ? MNC_EXECUTE : MNC_SELECT);
    }
    if (message == WM_UNINITMENUPOPUP) RestoreMenu((HMENU)wParam);
    if (message == WM_DRAWITEM && ((DRAWITEMSTRUCT*)lParam)->CtlType == ODT_MENU) { LRESULT result; if (DrawMenu(window, message, lParam, result)) return result; }
    if (message == WM_MEASUREITEM && ((MEASUREITEMSTRUCT*)lParam)->CtlType == ODT_MENU) { LRESULT result; if (DrawMenu(window, message, lParam, result)) return result; }
    if (message == WM_NOTIFY && ((NMHDR*)lParam)->code == NM_CUSTOMDRAW)
    {
        HWND from = ((NMHDR*)lParam)->hwndFrom; wchar_t type[64] = {}; GetClassName(from, type, 64);
        auto draw = (NMCUSTOMDRAW*)lParam;
        if (!_wcsicmp(type, WC_HEADER))
        {
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                RECT rc = draw->rc;
                Fill(draw->hdc, rc, draw->uItemState & CDIS_SELECTED ? palette.pressed : draw->uItemState & CDIS_HOT ? palette.hover : palette.raised);
                Frame(draw->hdc, rc, palette.border);
                wchar_t text[256] = {}; HDITEM item = {}; item.mask = HDI_TEXT | HDI_FORMAT; item.pszText = text; item.cchTextMax = 256;
                Header_GetItem(from, (int)draw->dwItemSpec, &item);
                int padding=GetProp(GetParent(from),L"ReaderPaddedList")?ReaderItemInset(rc.right-rc.left,GetWindowDpiCompat(from)):5;
                InflateRect(&rc,-padding,0);
                SetBkMode(draw->hdc, TRANSPARENT); SetTextColor(draw->hdc, IsWindowEnabled(from) ? palette.text : palette.disabled);
                if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN))
                {
                    RECT arrow = rc; arrow.left = arrow.right - MulDiv(16, GetWindowDpiCompat(from), 96); rc.right = arrow.left;
                    DrawText(draw->hdc, item.fmt & HDF_SORTUP ? L"▴" : L"▾", 1, &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                }
                UINT alignment = item.fmt & HDF_RIGHT ? DT_RIGHT : item.fmt & HDF_CENTER ? DT_CENTER : DT_LEFT;
                DrawText(draw->hdc, text, -1, &rc, alignment | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (draw->uItemState & CDIS_FOCUS) DrawFocusRect(draw->hdc, &rc);
                return CDRF_SKIPDEFAULT;
            }
        }
        if (!_wcsicmp(type, WC_TREEVIEW) || !_wcsicmp(type, WC_LISTVIEW))
        {
            LRESULT inherited = DefSubclassProc(window, message, wParam, lParam);
            if (draw->dwDrawStage==CDDS_ITEMPREPAINT && GetProp(from,L"ReaderReadingSidebar")) return DrawReadingRow(from,(NMTVCUSTOMDRAW*)lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) return inherited | CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && GetProp(from,L"ReaderPaddedList"))
            { if(inherited&CDRF_SKIPDEFAULT) return inherited;return DrawPaddedCell(from,(NMLVCUSTOMDRAW*)lParam); }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                bool selected = (draw->uItemState & CDIS_SELECTED) != 0;
                bool current = !_wcsicmp(type, WC_TREEVIEW) && (HTREEITEM)GetProp(from, L"ReaderCurrentChapter") == (HTREEITEM)draw->dwItemSpec;
                bool tree = !_wcsicmp(type, WC_TREEVIEW);
                bool active = tree && GetProp(from, L"ReaderCurrentChapter") ? current : selected;
                COLORREF foreground = !IsWindowEnabled(from) ? palette.disabled : active ? palette.selectedText : palette.text;
                COLORREF background = active ? palette.selected : selected || (draw->uItemState & CDIS_HOT) ? palette.hover : palette.surface;
                if (!_wcsicmp(type, WC_TREEVIEW)) { auto tree = (NMTVCUSTOMDRAW*)lParam; tree->clrText = foreground; tree->clrTextBk = background; }
                else { auto list = (NMLVCUSTOMDRAW*)lParam; list->clrText = foreground; list->clrTextBk = background; }
                // Only the paint flag changes; native selection, keyboard handling and
                // accessibility state stay intact while palette colors replace system blue.
                draw->uItemState &= ~CDIS_SELECTED;
            }
            if (draw->dwDrawStage==CDDS_ITEMPREPAINT && GetProp(from,L"ReaderPaddedList")) return inherited|CDRF_NOTIFYSUBITEMDRAW;
            return inherited;
        }
    }
    bool button = ClassIs(data, WC_BUTTON) && (GetWindowLong(window, GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW;
    bool combo = ClassIs(data, WC_COMBOBOX) && !(GetWindowLong(window, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE));
    bool status = ClassIs(data, STATUSCLASSNAME);
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && (button || combo || status))
    {
        PAINTSTRUCT ps = {}; HDC dc = message == WM_PAINT ? BeginPaint(window, &ps) : (HDC)wParam;
        if (button) PaintButton(window, dc, data); else if (combo) PaintCombo(window, dc); else PaintStatus(window, dc);
        if (message == WM_PAINT) EndPaint(window, &ps); return 0;
    }
    if (message == WM_ERASEBKGND && (button || combo || status)) return TRUE;
    if (message == WM_MOUSEMOVE && button && !data->hot) { data->hot = true; TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track); InvalidateRect(window, NULL, FALSE); }
    if (message == WM_MOUSELEAVE && button) { data->hot = false; InvalidateRect(window, NULL, FALSE); }
    LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (button || combo || status)
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE || message == BM_SETCHECK || message == BM_SETSTATE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP || message == SB_SETTEXT || message == CB_SETCURSEL || message == WM_SETTEXT || message == WM_THEMECHANGED)
            InvalidateRect(window, NULL, FALSE);
    return result;
}
} // namespace

const ReaderPalette& ThemeColors() { return palette; }
ReaderPalette ThemeControlPalette(HWND window)
{
    for(HWND ancestor=window;ancestor;ancestor=GetParent(ancestor))
        if(GetProp(ancestor,L"ReaderFocusRail"))
        {
            HIGHCONTRAST high={};high.cbSize=sizeof(high);
            bool highContrast=SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(high),&high,0)&&(high.dwFlags&HCF_HIGHCONTRASTON);
            ReaderPalette base=palette;
            if(highContrast)
            {
                base.window=base.surface=base.raised=base.hover=base.pressed=GetSysColor(COLOR_WINDOW);
                base.text=base.muted=base.accent=GetSysColor(COLOR_WINDOWTEXT);base.disabled=GetSysColor(COLOR_GRAYTEXT);
                base.selected=GetSysColor(COLOR_HIGHLIGHT);base.selectedText=GetSysColor(COLOR_HIGHLIGHTTEXT);base.border=GetSysColor(COLOR_WINDOWFRAME);
            }
            return ReaderFocusPalette(base,reading.background,GetProp(ancestor,L"ReaderRailClear")!=nullptr,highContrast);
        }
    return palette;
}
HBRUSH ThemeWindowBrush() { return windowBrush; }
ReaderScrollColors ThemeScrollbarColors(HWND window)
{
    wchar_t type[64]={};GetClassName(window,type,64);auto colors=ThemeControlPalette(window);
    bool content=!_wcsicmp(type,L"ReaderContentView")||GetProp(window,L"ReaderReadingEditor");
    uint32_t background=content?reading.background:!_wcsicmp(type,L"ReaderNavigationViewport")?colors.window:colors.surface;
    return ReaderScrollbarColors(colors,background);
}
ReaderColors ThemeReadingColors(const header_t* settings)
{
    if (!settings->reading_custom) return { palette.surface, palette.text, palette.text, false };
    return ResolveReaderColors(settings->ui_theme, true, settings->bg_color, settings->font_color,
        settings->use_same_font ? settings->font_color : settings->font_color_title, settings->bg_image.enable != 0);
}
void ThemeConfigure(const header_t* settings)
{
    palette = MakeReaderPalette(settings->ui_theme);
    HIGHCONTRAST high = {}; high.cbSize = sizeof(high);
    if (SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(high), &high, 0) && (high.dwFlags & HCF_HIGHCONTRASTON))
    {
        palette.window = palette.raised = GetSysColor(COLOR_BTNFACE); palette.surface = GetSysColor(COLOR_WINDOW);
        palette.text = GetSysColor(COLOR_WINDOWTEXT); palette.disabled = palette.muted = GetSysColor(COLOR_GRAYTEXT);
        palette.selected = palette.hover = palette.pressed = palette.accent = GetSysColor(COLOR_HIGHLIGHT);
        palette.selectedText = GetSysColor(COLOR_HIGHLIGHTTEXT); palette.border = GetSysColor(COLOR_WINDOWFRAME);
        palette.hit = palette.selected; palette.hitText = palette.selectedText;
    }
    reading = ThemeReadingColors(settings);
    if (!settings->reading_custom) { reading.background = palette.surface; reading.text = reading.title = palette.text; }
    if (windowBrush) DeleteObject(windowBrush); if (surfaceBrush) DeleteObject(surfaceBrush); if (readingBrush) DeleteObject(readingBrush);
    windowBrush = CreateSolidBrush(palette.window); surfaceBrush = CreateSolidBrush(palette.surface); readingBrush = CreateSolidBrush(reading.background);
    ThemeRefreshAll();
}
void ThemeAttachWindow(HWND window) { Attach(window, true); EnumChildWindows(window, AttachChild, 0); }
void ThemeReleaseMenus(HWND window)
{
    HMENU root = WindowMenu(window); if (!root) return;
    std::vector<HMENU> pending(1, root);
    for (size_t i = 0; i < pending.size(); ++i)
    {
        HMENU menu = pending[i];
        for (int n = 0; n < GetMenuItemCount(menu); ++n) { HMENU child = GetSubMenu(menu, n); if (child) pending.push_back(child); }
        RestoreMenu(menu);
    }
}
void ThemeUpdateMenu(HWND window)
{
    DWORD_PTR data = 0;
    if (GetWindowSubclass(window, WindowProc, subclassId, &data)) PrepareWindowMenu(window);
    if (GetMenu(window)) DrawMenuBar(window);
}
void ThemeRefreshAll()
{
    std::vector<HWND> windows(roots.begin(), roots.end());
    for (HWND window : windows) if (IsWindow(window)) { Attach(window, true); EnumChildWindows(window, AttachChild, 0); RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_NOERASE); }
}
void ThemeSetCurrentChapter(HWND tree, HTREEITEM item)
{
    HTREEITEM old = (HTREEITEM)GetProp(tree, L"ReaderCurrentChapter");
    if (old == item) return;
    SetProp(tree, L"ReaderCurrentChapter", item);
    RECT rc; if (old && TreeView_GetItemRect(tree, old, &rc, FALSE)) InvalidateRect(tree, &rc, FALSE);
    if (item && TreeView_GetItemRect(tree, item, &rc, FALSE)) InvalidateRect(tree, &rc, FALSE);
}
void ThemeShutdown()
{
    ReaderScrollbarsShutdown();
    while (!menus.empty()) RestoreMenu(menus.begin()->second->menu);
    roots.clear();
    if (windowBrush) DeleteObject(windowBrush); if (surfaceBrush) DeleteObject(surfaceBrush); if (readingBrush) DeleteObject(readingBrush);
    windowBrush = surfaceBrush = readingBrush = NULL;
}
