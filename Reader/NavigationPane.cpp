#include "NavigationPane.h"
#include "NavigationLayout.h"
#include "ReaderInteraction.h"
#include "Scrollbar.h"
#include "DPIAwareness.h"
#include "Theme.h"
#include "DrawReaderIcon.h"
#include "resource.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <vector>

namespace {
const UINT moreId=48100;
const UINT_PTR subclassId=0x524e4156;
const int commands[]={IDM_LIBRARY,IDM_VIEW,IDM_READER_SEARCH,IDM_MARK,IDM_DISPLAY,IDM_FOCUS,(int)moreId};
const wchar_t* labels[]={L"书库",L"阅读目录",L"查找",L"书签",L"显示与主题",L"专注阅读",L"更多"};
const ReaderIcon icons[]={ReaderIcon::Library,ReaderIcon::Toc,ReaderIcon::Search,ReaderIcon::Bookmark,ReaderIcon::Display,ReaderIcon::Focus,ReaderIcon::Menu};
HWND owner=NULL,pane=NULL,viewport=NULL,buttons[9]={},tips=NULL,previousFocus=NULL;
HMENU root=NULL;HFONT font=NULL,headingFont=NULL;UINT dpi=96;
ReaderNavigationGeometry geometry={};ReaderAltNavigationGesture altGesture;
int scrollPosition=0;bool keyboard=false,tracking=false,closing=false,finishing=false,focusMode=false;
LRESULT CALLBACK PaneProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK ViewportProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK ButtonProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
int Scale(int n) {return MulDiv(n,dpi,96);}
void RefreshEmphasis(HWND moved=nullptr)
{
    if(!pane) return;
    if(moved) {TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,moved,0};TrackMouseEvent(&track);}
    POINT mouse;GetCursorPos(&mouse);RECT bounds;GetWindowRect(pane,&bounds);
    bool clear=ReaderRailClear(focusMode,ReaderNavigationForeground()!=FALSE,PtInRect(&bounds,mouse)!=FALSE,
        ReaderNavigationContains(GetFocus())!=FALSE,tracking||finishing,ReaderNavigationContains(GetCapture())!=FALSE);
    bool previous=GetProp(pane,L"ReaderRailClear")!=nullptr;
    if(clear) SetProp(pane,L"ReaderRailClear",(HANDLE)1);else RemoveProp(pane,L"ReaderRailClear");
    if(clear!=previous) RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
}
void Position(HWND child,int x,int y,int width,int height)
{
    RECT old;GetWindowRect(child,&old);MapWindowPoints(NULL,GetParent(child),(POINT*)&old,2);
    if(old.left!=x || old.top!=y || old.right-old.left!=width || old.bottom-old.top!=height)
        SetWindowPos(child,NULL,x,y,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
}
void RememberFocus() {HWND focus=GetFocus();if(focus && !ReaderNavigationContains(focus)) previousFocus=focus;}
void Leave(bool restore)
{
    bool wasKeyboard=keyboard;keyboard=false;altGesture.Cancel();RemoveProp(owner,L"ReaderMenuKeyboard");
    if(restore && ReaderNavigationForeground())
        SetFocus(IsWindow(previousFocus)&&IsWindowVisible(previousFocus)&&IsWindowEnabled(previousFocus)?previousFocus:owner);
    if(wasKeyboard) SendMessage(owner,WM_READER_NAVIGATION_ACCESS,FALSE,0);
    if(ReaderNavigationForeground() && !IsWindowVisible(GetFocus())) SetFocus(owner);
    RefreshEmphasis();
}
void Enter()
{
    if(!pane) return;RememberFocus();keyboard=true;SetProp(owner,L"ReaderMenuKeyboard",(HANDLE)1);
    SendMessage(owner,WM_READER_NAVIGATION_ACCESS,TRUE,0);SetFocus(buttons[6]);
}
void Scroll(int position)
{
    scrollPosition=(std::max)(0,(std::min)(geometry.scrollMaximum,position));
    SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    info.nMin=0;info.nMax=geometry.contentHeight-1;info.nPage=geometry.viewportHeight;info.nPos=scrollPosition;
    SetScrollInfo(viewport,SB_VERT,&info,TRUE);
    RECT client;GetClientRect(viewport,&client);
    for(int i=0;i<7;++i) Position(buttons[i],0,i*geometry.itemHeight-scrollPosition,client.right,geometry.itemHeight);
    ReaderScrollbarsUpdate(viewport);
}
void EnsureFocusVisible(HWND button)
{
    for(int i=0;i<7;++i) if(button==buttons[i])
    {
        int top=i*geometry.itemHeight;
        if(top<scrollPosition) Scroll(top);
        else if(top+geometry.itemHeight>scrollPosition+geometry.viewportHeight) Scroll(top+geometry.itemHeight-geometry.viewportHeight);
        return;
    }
}
int FocusIndex() {for(int i=0;i<9;++i) if(GetFocus()==buttons[i]) return i;return -1;}
void FocusNext(int from,int direction)
{
    for(int n=0;n<9;++n) {from=ReaderNavigationCycle(9,from,direction);if(IsWindowEnabled(buttons[from])) {SetFocus(buttons[from]);return;}}
}
int Mnemonic(wchar_t key)
{
    for(int i=0;i<GetMenuItemCount(root);++i)
    {
        wchar_t text[128]={};GetMenuString(root,i,text,128,MF_BYPOSITION);
        if(ReaderMenuMnemonic(text)==towupper(key)) return i;
    }
    return -1;
}
HMENU BorrowMenu()
{
    HMENU popup=CreatePopupMenu();if(!popup) return NULL;
    for(int i=0;i<GetMenuItemCount(root);++i)
    {
        wchar_t text[128]={};GetMenuString(root,i,text,128,MF_BYPOSITION);
        HMENU child=GetSubMenu(root,i);UINT state=GetMenuState(root,i,MF_BYPOSITION);
        AppendMenu(popup,MF_STRING|(child?MF_POPUP:0)|(state&(MF_DISABLED|MF_GRAYED)),child?(UINT_PTR)child:GetMenuItemID(root,i),text);
    }
    return popup;
}
void OpenMore(bool fromKeyboard,int direct=-1)
{
    if(!pane || tracking || finishing || !root) return;
    if(fromKeyboard && !keyboard) Enter();else RememberFocus();
    altGesture.Cancel();SendMessage(owner,WM_INITMENU,(WPARAM)root,0);
    HMENU popup=direct>=0?GetSubMenu(root,direct):BorrowMenu();if(!popup) {Leave(true);return;}
    RECT anchor;GetWindowRect(buttons[6],&anchor);TPMPARAMS parameters={};parameters.cbSize=sizeof(parameters);GetWindowRect(pane,&parameters.rcExclude);
    tracking=true;closing=false;RefreshEmphasis();
    UINT command=TrackPopupMenuEx(popup,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD,anchor.right+Scale(4),anchor.top,owner,&parameters);
    tracking=false;finishing=true;
    if(direct<0) {while(GetMenuItemCount(popup)>0) RemoveMenu(popup,0,MF_BYPOSITION);DestroyMenu(popup);}
    if(!pane || !IsWindow(owner)) return;
    if(fromKeyboard && !command && !closing && ReaderNavigationForeground()) SetFocus(buttons[6]);else Leave(true);
    // The command still refers to the captured history IDs; rebuild afterward.
    if(command) PostMessage(owner,WM_COMMAND,command,0);
    PostMessage(owner,WM_READER_NATIVE_MENU_CLOSED,0,0);RefreshEmphasis();
}
void Fonts(UINT next)
{
    if(font && dpi==next) return;
    if(font) DeleteObject(font);if(headingFont) DeleteObject(headingFont);
    scrollPosition=MulDiv(scrollPosition,next,dpi);dpi=next;
    NONCLIENTMETRICS m={};m.cbSize=sizeof(m);SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(m),&m,0);
    m.lfMessageFont.lfHeight=-MulDiv(9,dpi,72);m.lfMessageFont.lfWidth=0;font=CreateFontIndirect(&m.lfMessageFont);
    m.lfMessageFont.lfHeight=-MulDiv(11,dpi,72);m.lfMessageFont.lfWeight=FW_SEMIBOLD;headingFont=CreateFontIndirect(&m.lfMessageFont);
    for(HWND button:buttons) SendMessage(button,WM_SETFONT,(WPARAM)font,TRUE);
}
void Invoke(HWND button)
{
    UINT command=GetDlgCtrlID(button);
    if(command==moreId) OpenMore(keyboard);else SendMessage(owner,WM_COMMAND,command,(LPARAM)button);
}
LRESULT CALLBACK ButtonProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR)
{
    if(message==WM_LBUTTONDOWN && !keyboard && !tracking) RememberFocus();
    if(message==WM_SETFOCUS) EnsureFocusVisible(window);
    if(message==WM_KILLFOCUS && !tracking && !ReaderNavigationContains((HWND)lParam)) Leave(false);
    if(message==WM_MOUSEWHEEL) {if(ReaderNavigationWheel(message,wParam,lParam)) return 0;return SendMessage(owner,message,wParam,lParam);}
    if(message==WM_MOUSEMOVE) RefreshEmphasis(window);
    if(message==WM_MOUSELEAVE || message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_CAPTURECHANGED || message==WM_LBUTTONUP) RefreshEmphasis();
    if(message==WM_KILLFOCUS || message==WM_CAPTURECHANGED || message==WM_LBUTTONUP) PostMessage(owner,WM_READER_NAVIGATION_REFRESH,0,0);
    if(message==WM_NCHITTEST) {LRESULT hit=SendMessage(owner,message,wParam,lParam);if(hit>=HTLEFT && hit<=HTBOTTOMRIGHT) return HTTRANSPARENT;}
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,ButtonProc,subclassId);
    return DefSubclassProc(window,message,wParam,lParam);
}
LRESULT CALLBACK ViewportProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    switch(message)
    {
    case WM_COMMAND: Invoke((HWND)lParam);return 0;
    case WM_MOUSEMOVE:RefreshEmphasis(window);return 0;
    case WM_MOUSELEAVE:RefreshEmphasis();return 0;
    case WM_ERASEBKGND:return TRUE;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);HBRUSH brush=CreateSolidBrush(ThemeControlPalette(window).window);FillRect(dc,&p.rcPaint,brush);DeleteObject(brush);EndPaint(window,&p);}return 0;
    case WM_MOUSEWHEEL:
        {POINT point={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};RECT bounds;GetWindowRect(pane,&bounds);
         if(!PtInRect(&bounds,point)) return SendMessage(owner,message,wParam,lParam);}
        Scroll(scrollPosition-MulDiv(GET_WHEEL_DELTA_WPARAM(wParam),geometry.itemHeight*2,WHEEL_DELTA));return 0;
    case WM_VSCROLL:
        {
            int next=scrollPosition;SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_TRACKPOS;GetScrollInfo(window,SB_VERT,&info);
            switch(LOWORD(wParam)) {case SB_LINEUP:next-=geometry.itemHeight;break;case SB_LINEDOWN:next+=geometry.itemHeight;break;
            case SB_PAGEUP:next-=geometry.viewportHeight;break;case SB_PAGEDOWN:next+=geometry.viewportHeight;break;
            case SB_TOP:next=0;break;case SB_BOTTOM:next=geometry.scrollMaximum;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:next=info.nTrackPos;break;}
            Scroll(next);
        }return 0;
    }
    return DefWindowProc(window,message,wParam,lParam);
}
LRESULT CALLBACK PaneProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    switch(message)
    {
    case WM_COMMAND:Invoke((HWND)lParam);return 0;
    case WM_MOUSEMOVE:RefreshEmphasis(window);return 0;
    case WM_MOUSELEAVE:RefreshEmphasis();return 0;
    case WM_ERASEBKGND:return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT rc;GetClientRect(window,&rc);auto colors=ThemeControlPalette(window);HBRUSH background=CreateSolidBrush(colors.window);FillRect(dc,&p.rcPaint,background);DeleteObject(background);
            RECT glyph={Scale(14),Scale(12),Scale(34),Scale(32)};if(!focusMode) DrawReaderIcon(dc,ReaderIcon::Open,glyph,colors.muted);
            if(!geometry.compact) {RECT label={Scale(48),0,rc.right-Scale(12),geometry.headerHeight};auto old=SelectObject(dc,headingFont);
                SetTextColor(dc,colors.text);SetBkMode(dc,TRANSPARENT);DrawText(dc,L"导航",-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);}
            RECT edge={rc.right-1,0,rc.right,rc.bottom};HBRUSH brush=CreateSolidBrush(colors.border);FillRect(dc,&edge,brush);DeleteObject(brush);EndPaint(window,&p);
        }return 0;
    case WM_MOUSEWHEEL:return SendMessage(viewport,message,wParam,lParam);
    case WM_LBUTTONDOWN:
        if(!(GetWindowLong(owner,GWL_STYLE)&WS_CAPTION)) {POINT p={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ClientToScreen(window,&p);SendMessage(owner,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(p.x,p.y));}return 0;
    case WM_NCHITTEST:{LRESULT hit=SendMessage(owner,message,wParam,lParam);if(hit>=HTLEFT && hit<=HTBOTTOMRIGHT) return HTTRANSPARENT;}break;
    }
    return DefWindowProc(window,message,wParam,lParam);
}
} // namespace
HMENU ReaderMenuHandle(HWND window) {HMENU menu=(HMENU)GetProp(window,L"ReaderMainMenu");return menu?menu:GetMenu(window);}
HWND ReaderNavigationWindow() {return pane;}
BOOL ReaderNavigationContains(HWND window) {return window && pane && (window==pane || IsChild(pane,window));}
BOOL ReaderNavigationForeground() {return GetForegroundWindow()==owner;}
BOOL ReaderNavigationTracking() {return tracking || finishing;}
void ReaderNavigationMenuClosed() {finishing=false;RefreshEmphasis();}
void RefreshReaderNavigationEmphasis() {RefreshEmphasis();}
BOOL ReaderNavigationKeyboardActive() {return keyboard;}
void LeaveReaderNavigation() {Leave(true);}
BOOL CreateReaderNavigation(HWND window,HMENU menu)
{
    owner=window;root=menu;
    WNDCLASS c={};c.lpfnWndProc=PaneProc;c.hInstance=(HINSTANCE)GetWindowLongPtr(owner,GWLP_HINSTANCE);c.lpszClassName=L"ReaderNavigationPane";c.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClass(&c);
    pane=CreateWindowEx(WS_EX_CONTROLPARENT,c.lpszClassName,L"主要导航",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,owner,NULL,c.hInstance,NULL);
    c.lpfnWndProc=ViewportProc;c.lpszClassName=L"ReaderNavigationViewport";RegisterClass(&c);
    viewport=pane?CreateWindowEx(WS_EX_CONTROLPARENT,c.lpszClassName,L"导航操作",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,pane,NULL,c.hInstance,NULL):NULL;
    if(!viewport) {if(pane) DestroyWindow(pane);pane=viewport=NULL;root=NULL;return FALSE;}
    tips=CreateWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,pane,NULL,c.hInstance,NULL);
    for(int i=0;i<9;++i)
    {
        bool action=i<7;const wchar_t* text=action?labels[i]:i==7?L"返回（Alt+Left）":L"前进（Alt+Right）";
        int command=action?commands[i]:ReaderNavigationCommand(i-7);
        bool check=command==IDM_LIBRARY || command==IDM_VIEW || command==IDM_MARK;
        buttons[i]=CreateWindow(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|(check?BS_RADIOBUTTON|BS_PUSHLIKE:BS_PUSHBUTTON),0,0,1,1,action?viewport:pane,(HMENU)(INT_PTR)command,c.hInstance,NULL);
        SetProp(buttons[i],action?L"ReaderSidebarItem":L"ReaderFlatIcon",(HANDLE)1);SetProp(buttons[i],L"ReaderIcon",(HANDLE)(INT_PTR)(action?icons[i]:i==7?ReaderIcon::Back:ReaderIcon::Forward));
        SetWindowSubclass(buttons[i],ButtonProc,subclassId,0);
        TOOLINFO tip={};tip.cbSize=sizeof(tip);tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=pane;tip.uId=(UINT_PTR)buttons[i];tip.lpszText=(wchar_t*)text;SendMessage(tips,TTM_ADDTOOL,0,(LPARAM)&tip);
    }
    SetWindowPos(viewport,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SetProp(owner,L"ReaderMainMenu",root);Fonts(GetWindowDpiCompat(owner));SetMenu(owner,NULL);ThemeAttachWindow(tips);return TRUE;
}
void LayoutReaderNavigation(int width,int height,BOOL visible,BOOL panel,BOOL focus)
{
    if(!pane) return;
    bool modeChanged=focusMode!=(focus!=FALSE);focusMode=focus!=FALSE;
    if(focusMode) SetProp(pane,L"ReaderFocusRail",(HANDLE)1);else RemoveProp(pane,L"ReaderFocusRail");
    Fonts(GetWindowDpiCompat(owner));auto next=ReaderNavigationLayout(width,height,dpi,visible!=FALSE,panel!=FALSE,focusMode);
    bool changed=modeChanged || next.compact!=geometry.compact;geometry=next;
    Position(pane,0,0,geometry.width,height);Position(viewport,0,geometry.headerHeight,geometry.width-1,geometry.viewportHeight);
    for(int i=0;i<7;++i) {if(geometry.compact) SetProp(buttons[i],L"ReaderCompact",(HANDLE)1);else RemoveProp(buttons[i],L"ReaderCompact");if(changed) InvalidateRect(buttons[i],NULL,FALSE);}
    Scroll(scrollPosition);
    if(modeChanged) {SetWindowText(buttons[5],focusMode?L"退出专注，固定侧栏（Esc）":L"专注阅读");TOOLINFO tip={};tip.cbSize=sizeof(tip);tip.hwnd=pane;tip.uId=(UINT_PTR)buttons[5];tip.lpszText=(wchar_t*)(focusMode?L"退出专注，固定侧栏（Esc）":labels[5]);SendMessage(tips,TTM_UPDATETIPTEXT,0,(LPARAM)&tip);}
    int size=Scale(28),left=geometry.compact?(geometry.width-size)/2:(geometry.width-Scale(64))/2;
    Position(buttons[7],left,geometry.footerTop+Scale(8),size,size);
    Position(buttons[8],geometry.compact?left:left+Scale(36),geometry.footerTop+Scale(geometry.compact?40:8),size,size);
    if((IsWindowVisible(pane)!=FALSE)!=(visible!=FALSE)) ShowWindow(pane,visible?SW_SHOWNA:SW_HIDE);
    if(changed) RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
    RefreshEmphasis();
}
void ReaderNavigationMenuChanged() {} // Native popup labels are read from the retained HMENU at opening.
void ReaderNavigationState(BOOL back,BOOL forward,BOOL editing,UINT active,BOOL ready)
{
    if(pane)
    {
        for(int i=0;i<9;++i)
        {
            BOOL dependent=i==1 || i==2 || i==3;
            BOOL enabled=i<7?((!editing || i==6) && (!dependent || ready)):ready&&!editing&&(i==7?back:forward);
            if((IsWindowEnabled(buttons[i])!=FALSE)!=(enabled!=FALSE)) EnableWindow(buttons[i],enabled);
            if(i<7 && (commands[i]==IDM_LIBRARY || commands[i]==IDM_VIEW || commands[i]==IDM_MARK))
            {UINT check=commands[i]==(int)active?BST_CHECKED:BST_UNCHECKED;if(SendMessage(buttons[i],BM_GETCHECK,0,0)!=check) SendMessage(buttons[i],BM_SETCHECK,check,0);}
        }
    }
    HMENU menu=root?root:ReaderMenuHandle(owner);if(menu) {EnableMenuItem(menu,IDM_NAV_BACK,MF_BYCOMMAND|(back&&ready&&!editing?MF_ENABLED:MF_GRAYED));EnableMenuItem(menu,IDM_NAV_FORWARD,MF_BYCOMMAND|(forward&&ready&&!editing?MF_ENABLED:MF_GRAYED));}
}
BOOL ReaderNavigationWheel(UINT message,WPARAM wParam,LPARAM lParam)
{
    if(!pane || !IsWindowVisible(pane)) return FALSE;
    POINT point={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};RECT bounds;GetWindowRect(pane,&bounds);
    if(!PtInRect(&bounds,point)) return FALSE;
    if(message==WM_MOUSEWHEEL) SendMessage(viewport,message,wParam,lParam);
    return TRUE;
}
BOOL ReaderNavigationTranslate(MSG* message)
{
    if(!pane || (message->hwnd!=owner && !IsChild(owner,message->hwnd) && !ReaderNavigationContains(message->hwnd))) return FALSE;
    bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(message->message==WM_KEYDOWN || message->message==WM_SYSKEYDOWN)
    {
        bool repeat=(message->lParam&(1L<<30))!=0;altGesture.Down((unsigned)message->wParam,control,repeat);
        auto action=ReaderNavigationKey((unsigned)message->wParam,alt,control,shift,ReaderNavigationContains(message->hwnd)!=FALSE);
        if(action==navigation_key_toggle) {if(!repeat) {if(keyboard) Leave(true);else Enter();}return TRUE;}
        if(action==navigation_key_leave) {Leave(true);if(ReaderNavigationContains(GetFocus())) SetFocus(owner);return TRUE;}
        if(action==navigation_key_previous || action==navigation_key_next) {FocusNext(FocusIndex(),action==navigation_key_previous?-1:1);return TRUE;}
        if(action==navigation_key_first || action==navigation_key_last) {FocusNext(action==navigation_key_first?8:0,action==navigation_key_first?1:-1);return TRUE;}
        if(action==navigation_key_activate) {if(!repeat && IsWindowEnabled(message->hwnd)) Invoke(message->hwnd);return TRUE;}
        if(message->wParam==VK_TAB && ReaderNavigationContains(message->hwnd) && !alt && !control)
        {
            int index=FocusIndex();
            if((shift&&index==0)||(!shift&&index==8)) {Leave(false);SetFocus(owner);}
            else FocusNext(index,shift?-1:1);
            return TRUE;
        }
        if(alt && !control && message->wParam>='A' && message->wParam<='Z') {int index=Mnemonic((wchar_t)message->wParam);if(index>=0) {Enter();OpenMore(true,index);return TRUE;}}
    }
    if(message->message==WM_SYSKEYUP && message->wParam==VK_MENU) {if(altGesture.Up(VK_MENU)) {if(keyboard) Leave(true);else Enter();}return TRUE;}
    return FALSE;
}
BOOL ReaderNavigationSystemMenu(WPARAM wParam,LPARAM lParam)
{
    if(!pane || (wParam&0xfff0)!=SC_KEYMENU || lParam==L' ') return FALSE;
    if(lParam) {int index=Mnemonic((wchar_t)lParam);if(index>=0) {Enter();OpenMore(true,index);return TRUE;}}
    Enter();return TRUE;
}
void CancelReaderNavigation() {closing=true;altGesture.Cancel();if(tracking) EndMenu();else Leave(false);}
void DestroyReaderNavigation()
{
    closing=true;if(tracking) EndMenu();if(pane) DestroyWindow(pane);pane=viewport=tips=NULL;
    if(font) DeleteObject(font);if(headingFont) DeleteObject(headingFont);font=headingFont=NULL;
    if(root) {RemoveProp(owner,L"ReaderMainMenu");DestroyMenu(root);root=NULL;}
    RemoveProp(owner,L"ReaderMenuKeyboard");owner=previousFocus=NULL;keyboard=tracking=finishing=focusMode=false;
}
