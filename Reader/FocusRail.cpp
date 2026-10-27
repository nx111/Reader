#include "FocusRail.h"
#include "Theme.h"
#include <windowsx.h>

namespace {
HWND owner=nullptr,pane=nullptr,host=nullptr;
HRGN original=nullptr;bool active=false,customRegion=false,following=false;
int railWidth=0,railHeight=0;RECT previousHole={},previousWindow={};BYTE opacity=255;
void SyncUiState(HWND parent)
{
    UINT flags=(UINT)SendMessage(owner,WM_QUERYUISTATE,0,0)&(UISF_HIDEFOCUS|UISF_HIDEACCEL);
    SendMessage(parent,WM_UPDATEUISTATE,MAKEWPARAM(UIS_SET,flags),0);
    SendMessage(parent,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,(UISF_HIDEFOCUS|UISF_HIDEACCEL)&~flags),0);
}
LRESULT CALLBACK HostProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    switch(message)
    {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return TRUE;
    case WM_CLOSE:return SendMessage(owner,WM_CLOSE,0,0);
    case WM_NCHITTEST:
        {LRESULT hit=SendMessage(owner,message,wParam,lParam);return hit>=HTLEFT&&hit<=HTBOTTOMRIGHT?hit:HTCLIENT;}
    case WM_NCLBUTTONDOWN:case WM_SETCURSOR:return SendMessage(owner,message,wParam,lParam);
    case WM_SYSCOMMAND:return SendMessage(owner,message,wParam,lParam);
    }
    return DefWindowProc(window,message,wParam,lParam);
}
void Restore()
{
    if(!active) return;
    active=false; // Region/parent APIs can synchronously send owner position events.
    SetParent(pane,owner);
    ShowWindow(host,SW_HIDE);
    SyncUiState(pane);
    // SetWindowRgn takes ownership only on success.
    if(SetWindowRgn(owner,customRegion?original:nullptr,TRUE)) {if(customRegion) original=nullptr;}
    else SetWindowRgn(owner,nullptr,TRUE);
    if(original) DeleteObject(original);original=nullptr;customRegion=false;
    SetRectEmpty(&previousHole);SetRectEmpty(&previousWindow);
}
} // namespace
BOOL ReaderFocusRailLayout(HWND window,HWND navigation,int width,int height,BOOL focus)
{
    owner=window;pane=navigation;railWidth=width;railHeight=height;
    if(!focus) {Restore();return FALSE;}
    if(!active)
    {
        if(!host)
        {
            WNDCLASS cls={};cls.lpfnWndProc=HostProc;cls.hInstance=(HINSTANCE)GetWindowLongPtr(owner,GWLP_HINSTANCE);
            cls.lpszClassName=L"ReaderFocusRailHost";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClass(&cls);
            host=CreateWindowEx(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,cls.lpszClassName,L"阅读导航",
                WS_POPUP|WS_CLIPCHILDREN,0,0,1,1,owner,nullptr,cls.hInstance,nullptr);
            if(!host) return FALSE;
            SetProp(host,L"ReaderOwnBackground",(HANDLE)1);
        }
        if(!SetLayeredWindowAttributes(host,0,opacity,LWA_ALPHA)) return FALSE;
        original=CreateRectRgn(0,0,0,0);if(!original) return FALSE;
        customRegion=GetWindowRgn(owner,original)!=ERROR;
        SetLastError(0);HWND old=SetParent(pane,host);
        if(!old) {DeleteObject(original);original=nullptr;customRegion=false;return FALSE;}
        active=true;SyncUiState(host);ThemeAttachWindow(host);
    }
    ReaderFocusRailFollow();return active;
}
void ReaderFocusRailFollow()
{
    if(!active || following) return;following=true;
    RECT bounds;GetWindowRect(owner,&bounds);POINT client={0,0};ClientToScreen(owner,&client);
    RECT size={0,0,bounds.right-bounds.left,bounds.bottom-bounds.top};
    RECT hole={client.x-bounds.left,client.y-bounds.top,client.x-bounds.left+railWidth,client.y-bounds.top+railHeight};
    if(!EqualRect(&hole,&previousHole)||!EqualRect(&size,&previousWindow))
    {
        HRGN region=CreateRectRgn(0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
        HRGN cut=CreateRectRgn(hole.left,hole.top,hole.right,hole.bottom);
        bool ok=region&&cut&&CombineRgn(region,region,cut,RGN_DIFF)!=ERROR;
        if(cut) DeleteObject(cut);
        if(ok && SetWindowRgn(owner,region,TRUE)) {previousHole=hole;previousWindow=size;}
        else {if(region) DeleteObject(region);Restore();following=false;return;}
    }
    bool visible=IsWindowVisible(owner)&&!IsIconic(owner);
    EnableWindow(host,IsWindowEnabled(owner));
    SetWindowPos(host,nullptr,client.x,client.y,railWidth,railHeight,SWP_NOACTIVATE|SWP_NOZORDER|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
    following=false;
}
void ReaderFocusRailEmphasis(BOOL clear)
{
    HIGHCONTRAST high={};high.cbSize=sizeof(high);
    bool contrast=SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(high),&high,0)&&(high.dwFlags&HCF_HIGHCONTRASTON);
    BYTE next=clear||contrast?255:176;
    if(opacity!=next) {opacity=next;if(active) SetLayeredWindowAttributes(host,0,opacity,LWA_ALPHA);}
}
BOOL ReaderFocusRailContains(HWND window)
{return active&&window&&(window==host||IsChild(host,window));}
void ReaderFocusRailDestroy()
{
    Restore();if(host) DestroyWindow(host);host=owner=pane=nullptr;opacity=255;
}
