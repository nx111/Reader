#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
using BOOL=int;using BYTE=unsigned char;using DWORD=unsigned;using UINT=unsigned;using WPARAM=uintptr_t;using LPARAM=intptr_t;using LRESULT=intptr_t;using LONG_PTR=intptr_t;using HWND=int;using HINSTANCE=int;using HCURSOR=int;using HANDLE=void*;
struct RECT {int left=0,top=0,right=0,bottom=0;};struct POINT {int x,y;};
struct Region {RECT full,hole;};using HRGN=Region*;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define nullptr 0
#define MAKEWPARAM(a,b) ((a)|((b)<<16))
enum {WM_QUERYUISTATE=100,WM_UPDATEUISTATE,UIS_SET=1,UIS_CLEAR=2,UISF_HIDEFOCUS=1,UISF_HIDEACCEL=2};
enum {ERROR=0,WM_MOUSEACTIVATE=1,WM_ERASEBKGND,WM_CLOSE,WM_NCHITTEST,WM_NCLBUTTONDOWN,WM_SETCURSOR,WM_SYSCOMMAND,MA_NOACTIVATE=8,HTCLIENT=1,HTLEFT=10,HTBOTTOMRIGHT=17,SW_HIDE=0,GWLP_HINSTANCE=1,IDC_ARROW=2,WS_EX_TOOLWINDOW=1,WS_EX_NOACTIVATE=2,WS_EX_LAYERED=4,WS_POPUP=8,WS_CLIPCHILDREN=16,LWA_ALPHA=1,RGN_DIFF=4,SWP_NOACTIVATE=1,SWP_NOZORDER=2,SWP_SHOWWINDOW=4,SWP_HIDEWINDOW=8,SPI_GETHIGHCONTRAST=1,HCF_HIGHCONTRASTON=1};
struct WNDCLASS {LRESULT (*lpfnWndProc)(HWND,UINT,WPARAM,LPARAM);HINSTANCE hInstance;const wchar_t* lpszClassName;HCURSOR hCursor;};
struct HIGHCONTRAST {unsigned cbSize=0,dwFlags=0;};
struct FakeWindow {RECT bounds={100,200,1100,900};HWND parent=0;bool shown=true,enabled=true,iconic=false;BYTE alpha=255;HRGN region=0;};
static std::map<HWND,FakeWindow> windows;static int regions=0;static bool high=false;static std::string fail;static void (*regionCallback)()=0;
inline HRGN CreateRectRgn(int l,int t,int r,int b) {++regions;return new Region{{l,t,r,b},{}};}
inline void DeleteObject(HRGN r) {if(r){--regions;delete r;}}
inline BOOL SetWindowRgn(HWND w,HRGN r,BOOL) {if(fail=="region"){fail.clear();return FALSE;}DeleteObject(windows[w].region);windows[w].region=r;if(regionCallback)regionCallback();return TRUE;}
inline int GetWindowRgn(HWND w,HRGN r) {if(!windows[w].region)return ERROR;*r=*windows[w].region;return 1;}
inline int CombineRgn(HRGN dst,HRGN src,HRGN hole,int) {dst->full=src->full;dst->hole=hole->full;return 1;}
inline BOOL EqualRect(const RECT* a,const RECT* b) {return a->left==b->left&&a->top==b->top&&a->right==b->right&&a->bottom==b->bottom;}
inline void SetRectEmpty(RECT* r) {*r={};}
inline void ShowWindow(HWND w,int) {windows[w].shown=false;}
inline HWND SetParent(HWND w,HWND p) {HWND old=windows[w].parent;windows[w].parent=p;return old;}
inline intptr_t GetWindowLongPtr(HWND,int) {return 1;}
inline int LoadCursor(int,int) {return 1;}inline void RegisterClass(WNDCLASS*) {}
inline HWND CreateWindowEx(int,const wchar_t*,const wchar_t*,int,int,int,int,int,HWND p,int,int,void*) {if(fail=="create")return 0;windows[3]=FakeWindow();windows[3].parent=p;return 3;}
inline BOOL SetLayeredWindowAttributes(HWND w,int,BYTE alpha,int) {if(fail=="layered")return FALSE;windows[w].alpha=alpha;return TRUE;}
inline void SetProp(HWND,const wchar_t*,HANDLE) {}inline void SetLastError(int) {}inline DWORD GetLastError() {return 0;}
inline void GetWindowRect(HWND w,RECT* r) {*r=windows[w].bounds;}
inline void ClientToScreen(HWND w,POINT* p) {p->x+=windows[w].bounds.left+8;p->y+=windows[w].bounds.top+30;}
inline BOOL IsWindowVisible(HWND w) {return windows[w].shown;}inline BOOL IsIconic(HWND w) {return windows[w].iconic;}
inline BOOL IsWindowEnabled(HWND w) {return windows[w].enabled;}inline void EnableWindow(HWND w,BOOL value) {windows[w].enabled=value;}
inline void SetWindowPos(HWND w,int,int x,int y,int width,int height,int flags) {windows[w].bounds={x,y,x+width,y+height};if(flags&SWP_SHOWWINDOW)windows[w].shown=true;if(flags&SWP_HIDEWINDOW)windows[w].shown=false;}
inline BOOL SystemParametersInfo(int,unsigned,HIGHCONTRAST* p,int) {p->dwFlags=high?HCF_HIGHCONTRASTON:0;return TRUE;}
inline BOOL IsChild(HWND p,HWND w) {while(w&&windows[w].parent!=w){w=windows[w].parent;if(w==p)return TRUE;}return FALSE;}
inline void DestroyWindow(HWND w) {windows[w].shown=false;DeleteObject(windows[w].region);windows[w].region=0;}
inline LRESULT SendMessage(HWND,UINT,WPARAM,LPARAM) {return HTCLIENT;}
inline LRESULT DefWindowProc(HWND,UINT,WPARAM,LPARAM) {return 0;}
inline void ThemeAttachWindow(HWND) {}
