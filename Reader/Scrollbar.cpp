#include "Scrollbar.h"
#include "ScrollbarModel.h"
#include "Theme.h"
#include "DPIAwareness.h"
#include <commctrl.h>
#include <oleacc.h>
#include <windowsx.h>
#include <map>
#include <memory>
#include <vector>

#pragma comment(lib,"Oleacc.lib")
#pragma comment(lib,"Uuid.lib")

namespace {
const UINT_PTR subclassId=0x52534352,timerId=0x52534644;
const wchar_t* stripClass=L"ReaderScrollStrip";
struct Host;
struct Strip
{
    Host* host=nullptr;HWND window=nullptr;int axis=0;
    bool range=false,pinned=false,timer=false;ReaderScrollbarThumb thumb;
    ReaderScrollbarVisibility visual;
};
struct Host
{
    HWND target=nullptr;bool syncing=false,alive=true;Strip bars[3];
};
std::map<HWND,std::shared_ptr<Host>> hosts;
LRESULT CALLBACK StripProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK TargetProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
bool HighContrast()
{ HIGHCONTRAST high={};high.cbSize=sizeof(high);return SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(high),&high,0)&&(high.dwFlags&HCF_HIGHCONTRASTON); }
bool Active(HWND target)
{return GetAncestor(GetForegroundWindow(),GA_ROOTOWNER)==GetAncestor(target,GA_ROOTOWNER);}
void Animate(Strip& bar)
{
    bool needed=bar.visual.Animate();
    if(needed&&!bar.timer) bar.timer=SetTimer(bar.window,timerId,16,nullptr)!=0;
    if(!needed&&bar.timer) {KillTimer(bar.window,timerId);bar.timer=false;}
}
void Reset(Strip& bar)
{
    bar.pinned=false;bar.visual.Reset();
    if(bar.timer) KillTimer(bar.window,timerId);bar.timer=false;
    if(bar.window) InvalidateRect(bar.window,nullptr,FALSE);
}
void Pointer(Strip& bar)
{
    if(!bar.window || !bar.host->alive) return;
    if(!bar.range || !Active(bar.host->target)) {Reset(bar);return;}
    POINT point;GetCursorPos(&point);RECT bounds;GetWindowRect(bar.window,&bounds);
    bool inside=PtInRect(&bounds,point)!=FALSE;
    ScreenToClient(bar.window,&point);auto before=bar.visual;
    bar.visual.Drag(bar.pinned || GetCapture()==bar.host->target,GetTickCount64());
    bar.visual.Pointer(inside,bar.thumb.Contains(bar.axis==SB_VERT?point.y:point.x),GetTickCount64());
    if(before.opacity!=bar.visual.opacity || before.thumb!=bar.visual.thumb || before.drag!=bar.visual.drag)
        InvalidateRect(bar.window,nullptr,FALSE);
    Animate(bar);
}
void Position(Strip& bar,RECT screen,bool show)
{
    if(!bar.window) return;
    if(!show)
    { Reset(bar);if(IsWindowVisible(bar.window)) ShowWindow(bar.window,SW_HIDE);return; }
    RECT old;GetWindowRect(bar.window,&old);
    if(!EqualRect(&old,&screen) || !IsWindowVisible(bar.window))
    {
        MapWindowPoints(nullptr,GetParent(bar.window),(POINT*)&screen,2);
        SetWindowPos(bar.window,HWND_TOP,screen.left,screen.top,screen.right-screen.left,screen.bottom-screen.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
}
void Sync(const std::shared_ptr<Host>& host)
{
    if(!host->alive || host->syncing) return;
    host->syncing=true;
    bool visible=IsWindowVisible(host->target)&&!HighContrast();RECT bounds[2]={};bool shown[2]={};
    for(int axis:{SB_HORZ,SB_VERT})
    {
        auto& bar=host->bars[axis];SCROLLBARINFO native={};native.cbSize=sizeof(native);
        SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
        bool exists=visible&&GetScrollBarInfo(host->target,axis==SB_VERT?OBJID_VSCROLL:OBJID_HSCROLL,&native)
            && !(native.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)) && !IsRectEmpty(&native.rcScrollBar);
        shown[axis]=exists;bounds[axis]=native.rcScrollBar;
        bar.range=exists&&IsWindowEnabled(host->target)&&GetScrollInfo(host->target,axis,&info)&&ReaderScrollHasRange(info.nMin,info.nMax,info.nPage)
            && !(native.rgstate[0]&STATE_SYSTEM_UNAVAILABLE);
        int length=axis==SB_VERT?native.rcScrollBar.bottom-native.rcScrollBar.top:native.rcScrollBar.right-native.rcScrollBar.left;
        auto thumb=ReaderScrollbarNativeThumb(length,native.xyThumbTop,native.xyThumbBottom);
        bool moved=bar.thumb.start!=thumb.start || bar.thumb.end!=thumb.end;
        bar.thumb=thumb;Position(bar,native.rcScrollBar,exists);Pointer(bar);
        if(moved&&bar.window) InvalidateRect(bar.window,nullptr,FALSE);
    }
    RECT corner={bounds[SB_VERT].left,bounds[SB_HORZ].top,bounds[SB_VERT].right,bounds[SB_HORZ].bottom};
    Position(host->bars[2],corner,shown[0]&&shown[1]&&!IsRectEmpty(&corner));
    host->syncing=false;
}
void Paint(Strip& bar,HDC dc)
{
    RECT rect;GetClientRect(bar.window,&rect);auto colors=ThemeScrollbarColors(bar.host->target);
    HBRUSH brush=CreateSolidBrush(colors.background);FillRect(dc,&rect,brush);DeleteObject(brush);
    if(!bar.range || !bar.visual.opacity || bar.axis==2) return;
    brush=CreateSolidBrush(ReaderBlendColor(colors.track,colors.background,bar.visual.opacity));FillRect(dc,&rect,brush);DeleteObject(brush);
    RECT thumb=rect;int width=ReaderScrollbarThickness(bar.axis==SB_VERT?rect.right:rect.bottom,GetWindowDpiCompat(bar.host->target));
    if(bar.axis==SB_VERT) {thumb.top=bar.thumb.start;thumb.bottom=bar.thumb.end;thumb.left+=(rect.right-width)/2;thumb.right=thumb.left+width;}
    else {thumb.left=bar.thumb.start;thumb.right=bar.thumb.end;thumb.top+=(rect.bottom-width)/2;thumb.bottom=thumb.top+width;}
    COLORREF ink=bar.visual.drag?colors.pressed:bar.visual.thumb?colors.hot:colors.thumb;
    brush=CreateSolidBrush(ReaderBlendColor(ink,colors.background,bar.visual.opacity));
    HGDIOBJ previous=SelectObject(dc,brush),pen=SelectObject(dc,GetStockObject(NULL_PEN));
    RoundRect(dc,thumb.left,thumb.top,thumb.right,thumb.bottom,width,width);
    SelectObject(dc,pen);SelectObject(dc,previous);DeleteObject(brush);
}
LRESULT Track(Strip& bar,UINT message,WPARAM wParam,LPARAM lParam)
{
    if(!bar.range || !bar.host->alive) return 0;
    bar.pinned=true;Pointer(bar);UpdateWindow(bar.window);
    POINT point={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ClientToScreen(bar.window,&point);
    // Forward into the real native tracking loop. It owns capture, 32-bit
    // nTrackPos, repeat/page/line actions, touch promotion and cancellation.
    LRESULT result=SendMessage(bar.host->target,message,bar.axis==SB_VERT?HTVSCROLL:HTHSCROLL,MAKELPARAM(point.x,point.y));
    bar.pinned=false;
    if(bar.host->alive) Pointer(bar);
    return result;
}
LRESULT CALLBACK StripProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    Strip* bar=(Strip*)GetWindowLongPtr(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {bar=(Strip*)((CREATESTRUCT*)lParam)->lpCreateParams;SetWindowLongPtr(window,GWLP_USERDATA,(LONG_PTR)bar);bar->window=window;}
    if(!bar) return DefWindowProc(window,message,wParam,lParam);
    auto found=hosts.find(bar->host->target);std::shared_ptr<Host> keep=found==hosts.end()?nullptr:found->second;
    switch(message)
    {
    case WM_ERASEBKGND:return TRUE;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);Paint(*bar,dc);EndPaint(window,&ps);}return 0;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_NCHITTEST:if(!bar->range || bar->axis==2) return HTTRANSPARENT;return HTCLIENT;
    case WM_MOUSEMOVE:{TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);Pointer(*bar);}return 0;
    case WM_MOUSELEAVE:Pointer(*bar);return 0;
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:return Track(*bar,WM_NCLBUTTONDOWN,wParam,lParam);
    case WM_RBUTTONDOWN:return Track(*bar,WM_NCRBUTTONDOWN,wParam,lParam);
    case WM_RBUTTONUP:return Track(*bar,WM_NCRBUTTONUP,wParam,lParam);
    case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:return SendMessage(bar->host->target,message,wParam,lParam);
    case WM_GETOBJECT:
        if((LONG)lParam==OBJID_CLIENT && bar->axis!=2)
        {
            IAccessible* accessible=nullptr;
            if(SUCCEEDED(AccessibleObjectFromWindow(bar->host->target,bar->axis==SB_VERT?OBJID_VSCROLL:OBJID_HSCROLL,IID_IAccessible,(void**)&accessible)))
            {LRESULT value=LresultFromObject(IID_IAccessible,wParam,accessible);accessible->Release();return value;}
        }break;
    case WM_TIMER:
        if(wParam==timerId)
        {
            Pointer(*bar);unsigned previous=bar->visual.opacity;bar->visual.Tick(GetTickCount64());
            if(previous!=bar->visual.opacity) InvalidateRect(window,nullptr,FALSE);Animate(*bar);return 0;
        }break;
    case WM_CANCELMODE:Reset(*bar);return 0;
    case WM_NCDESTROY:Reset(*bar);bar->window=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);break;
    }
    return DefWindowProc(window,message,wParam,lParam);
}
LRESULT CALLBACK TargetProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR)
{
    auto found=hosts.find(window);if(found==hosts.end()) return DefSubclassProc(window,message,wParam,lParam);
    auto host=found->second;
    if(message==WM_NCDESTROY)
    {
        host->alive=false;hosts.erase(window);RemoveWindowSubclass(window,TargetProc,subclassId);
        for(auto& bar:host->bars) if(bar.window) DestroyWindow(bar.window);
        return DefSubclassProc(window,message,wParam,lParam);
    }
    LRESULT result=DefSubclassProc(window,message,wParam,lParam);
    if(message==WM_CANCELMODE) for(auto& bar:host->bars) Reset(bar);
    // Includes native control inserts, deletes, selection/ensure-visible,
    // keyboard/wheel and paint. No polling and no replacement scroll state.
    Sync(host);return result;
}
} // namespace

void ReaderScrollbarsAttach(HWND window)
{
    if(!window || !(GetWindowLong(window,GWL_STYLE)&WS_CHILD)) return;
    wchar_t type[64]={};GetClassName(window,type,64);
    if(!_wcsicmp(type,L"EDIT") && !(GetWindowLong(window,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL))) return;
    if(_wcsicmp(type,WC_TREEVIEW)&&_wcsicmp(type,WC_LISTVIEW)&&_wcsicmp(type,L"LISTBOX")&&_wcsicmp(type,L"EDIT")
        &&_wcsicmp(type,L"ReaderNavigationViewport")&&_wcsicmp(type,L"ReaderContentView")) return;
    auto existing=hosts.find(window);if(existing!=hosts.end()) {Sync(existing->second);return;}
    WNDCLASS cls={};cls.lpfnWndProc=StripProc;cls.hInstance=(HINSTANCE)GetWindowLongPtr(window,GWLP_HINSTANCE);cls.lpszClassName=stripClass;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.style=CS_DBLCLKS;RegisterClass(&cls);
    auto host=std::make_shared<Host>();host->target=window;hosts[window]=host;
    for(int axis=0;axis<3;++axis)
    {
        auto& bar=host->bars[axis];bar.host=host.get();bar.axis=axis;
        bar.window=CreateWindowEx(WS_EX_NOACTIVATE,stripClass,axis==SB_VERT?L"垂直滚动":axis==SB_HORZ?L"水平滚动":L"",WS_CHILD|WS_CLIPSIBLINGS,
            0,0,1,1,GetParent(window),nullptr,cls.hInstance,&bar);
        if(!bar.window)
        {for(auto& created:host->bars) if(created.window) DestroyWindow(created.window);hosts.erase(window);return;}
    }
    if(!SetWindowSubclass(window,TargetProc,subclassId,0))
    {for(auto& bar:host->bars) DestroyWindow(bar.window);hosts.erase(window);return;}
    Sync(host);
}
void ReaderScrollbarsUpdate(HWND window)
{auto found=hosts.find(window);if(found!=hosts.end()) Sync(found->second);}
void ReaderScrollbarsRefresh(HWND root,BOOL cancel)
{
    std::vector<std::shared_ptr<Host>> copy;for(auto& item:hosts) if(item.first==root||IsChild(root,item.first)) copy.push_back(item.second);
    for(auto& host:copy)
    {
        if(cancel) {for(auto& bar:host->bars) Reset(bar);if(GetCapture()==host->target) ReleaseCapture();}
        Sync(host);for(auto& bar:host->bars) if(bar.window) InvalidateRect(bar.window,nullptr,FALSE);
    }
}
void ReaderScrollbarsShutdown()
{
    auto copy=hosts;hosts.clear();
    for(auto& item:copy)
    {auto& host=*item.second;host.alive=false;RemoveWindowSubclass(item.first,TargetProc,subclassId);for(auto& bar:host.bars) if(bar.window) DestroyWindow(bar.window);}
}
