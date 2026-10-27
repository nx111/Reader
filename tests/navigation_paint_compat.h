#pragma once
#include "NavigationLayout.h"
#include "ReaderInteraction.h"
#include "ThemePalette.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
using HWND=void*;using HANDLE=void*;using UINT=unsigned;using UINT_PTR=uintptr_t;using LPARAM=intptr_t;using BOOL=int;
struct RECT {int left=0,top=0,right=0,bottom=0;};struct POINT {int x=0,y=0;};
struct TRACKMOUSEEVENT {unsigned cbSize,dwFlags;HWND hwndTrack;unsigned dwHoverTime;};
struct TOOLINFO {unsigned cbSize=0;HWND hwnd=nullptr;UINT_PTR uId=0;wchar_t* lpszText=nullptr;};
struct HIGHCONTRAST {unsigned cbSize=0,dwFlags=0;};
enum {FALSE=0,TRUE=1,TME_LEAVE=1,RDW_INVALIDATE=1,RDW_ALLCHILDREN=2,RDW_NOERASE=4,SW_SHOWNA=8,SW_HIDE=0,SPI_GETHIGHCONTRAST=1,HCF_HIGHCONTRASTON=1,TTM_UPDATETIPTEXT=1};
enum {COLOR_WINDOW,COLOR_WINDOWTEXT,COLOR_GRAYTEXT,COLOR_HIGHLIGHT,COLOR_HIGHLIGHTTEXT,COLOR_WINDOWFRAME};
inline uint32_t GetSysColor(int index){const uint32_t colors[]={0x020202,0xfafafa,0x777777,0x222277,0xffffff,0xdddddd};return colors[index];}
struct FakeWindow {HWND parent=nullptr;RECT rect;bool shown=true,iconic=false;std::wstring name;};
static HWND owner=(HWND)1,pane=(HWND)2,viewport=(HWND)3,buttons[9]={(HWND)4,(HWND)5,(HWND)6,(HWND)7,(HWND)8,(HWND)9,(HWND)10,(HWND)11,(HWND)12},tips=(HWND)13;
static HWND foreground=owner,focus=nullptr,capture=nullptr;static POINT mouse={2000,2000};
static std::map<HWND,FakeWindow> windows;
static std::map<std::pair<HWND,std::wstring>,HANDLE> properties;
static std::vector<HWND> dirties;static UINT dpi=96;static bool focusMode=false,highContrast=false,tracking=false,finishing=false;
static ReaderNavigationGeometry geometry={};static int scrollPosition=0,anchor=12345;
static ReaderPalette palette=MakeReaderPalette(0);static ReaderColors reading={0xffffff,0,0,false};
const wchar_t* labels[]={L"书库",L"阅读目录",L"查找",L"书签",L"显示与主题",L"专注阅读",L"更多"};
inline void GetCursorPos(POINT* p) {*p=mouse;}inline HWND GetForegroundWindow(){return foreground;}inline HWND GetFocus(){return focus;}inline HWND GetCapture(){return capture;}
inline BOOL IsChild(HWND parent,HWND child){while(child){child=windows[child].parent;if(child==parent)return TRUE;}return FALSE;}
inline HWND GetParent(HWND window){return windows[window].parent;}
inline BOOL ReaderNavigationContains(HWND w){return w&&(w==pane||IsChild(pane,w));}inline BOOL ReaderNavigationForeground(){return foreground==owner;}
inline void GetWindowRect(HWND w,RECT* r){*r=windows[w].rect;for(HWND p=windows[w].parent;p;p=windows[p].parent){int x=windows[p].rect.left,y=windows[p].rect.top;r->left+=x;r->right+=x;r->top+=y;r->bottom+=y;}}
inline BOOL PtInRect(const RECT* r,POINT p){return p.x>=r->left&&p.x<r->right&&p.y>=r->top&&p.y<r->bottom;}
inline HANDLE GetProp(HWND w,const wchar_t* name){return properties[{w,name}];}inline void SetProp(HWND w,const wchar_t* n,HANDLE v){properties[{w,n}]=v;}inline void RemoveProp(HWND w,const wchar_t* n){properties.erase({w,n});}
inline void TrackMouseEvent(TRACKMOUSEEVENT*){}inline void RedrawWindow(HWND w,void*,void*,unsigned){dirties.push_back(w);}inline void InvalidateRect(HWND w,void*,BOOL){dirties.push_back(w);}
inline BOOL IsWindowVisible(HWND w){for(HWND p=w;p;p=windows[p].parent)if(!windows[p].shown||windows[p].iconic)return FALSE;return TRUE;}
inline void ShowWindow(HWND w,int mode){windows[w].shown=mode!=SW_HIDE;}inline void SetWindowText(HWND w,const wchar_t* s){windows[w].name=s;}
inline void SendMessage(HWND,unsigned,uintptr_t,LPARAM){}inline UINT GetWindowDpiCompat(HWND){return dpi;}
inline void Fonts(UINT){}inline int Scale(int value){return ReaderScale(value,dpi);}
inline void Position(HWND w,int x,int y,int width,int height){windows[w].rect={x,y,x+width,y+height};}
inline void Scroll(int position){scrollPosition=(std::max)(0,(std::min)(geometry.scrollMaximum,position));}
inline BOOL SystemParametersInfo(int,unsigned,HIGHCONTRAST* high,unsigned){high->dwFlags=highContrast?HCF_HIGHCONTRASTON:0;return TRUE;}
inline void InitRail(){windows[owner].rect={100,200,1100,900};windows[pane].parent=owner;windows[viewport].parent=pane;for(int i=0;i<9;++i)windows[buttons[i]].parent=i<7?viewport:pane;}
