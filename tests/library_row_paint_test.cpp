#include "ThemePalette.h"
#include "ReaderInteraction.h"
#include <algorithm>
#include <cassert>
#include <cwchar>
#include <string>
#include <vector>

using HWND=int;using HANDLE=void*;using HFONT=void*;using HGDIOBJ=void*;
using UINT=unsigned;using COLORREF=uint32_t;using LRESULT=intptr_t;using LPARAM=intptr_t;
struct RECT {int left,top,right,bottom;};struct POINT {int x,y;};
struct Text {RECT bounds,clip;COLORREF color;HFONT font;std::wstring text;};
struct FillOp {RECT bounds;COLORREF color;};
struct State {RECT clip={0,0,1000000,1000000};HFONT font=nullptr;COLORREF color=0;};
struct Canvas {State state;std::vector<State> saved;std::vector<FillOp> fills;std::vector<Text> text;std::vector<RECT> focus;};
using HDC=Canvas*;
struct NMCUSTOMDRAW {HDC hdc;uintptr_t dwItemSpec;};
struct NMLVCUSTOMDRAW {NMCUSTOMDRAW nmcd;int iSubItem;};
struct LVITEM {int iSubItem;wchar_t* pszText;int cchTextMax;};
const UINT LVIS_SELECTED=2,LVIS_FOCUSED=1,LVIR_BOUNDS=0,WM_GETFONT=49,LVM_GETITEMTEXT=100,WM_QUERYUISTATE=297,UISF_HIDEFOCUS=1;
const int CDRF_DODEFAULT=0,CDRF_SKIPDEFAULT=4,CDRF_SKIPPOSTPAINT=256,TRANSPARENT=1;
const UINT DT_LEFT=0,DT_SINGLELINE=32,DT_VCENTER=4,DT_NOPREFIX=2048,DT_END_ELLIPSIS=32768;
static ReaderPalette palette;
static bool library=true,enabled=true,hideFocus=false,validItem=true;
static int dpi=96,scrollX=0,clientWidth=700,focusWindow=1;
static UINT states[3]={LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED,0};
static const int widths[]={205,120,65,110,145,100,350};
static int Scale(int value){return value*dpi/96;}
static int Left(int column){int left=-scrollX;for(int i=0;i<column;++i)left+=Scale(widths[i]);return left;}
static RECT Cell(int row,int column){return {Left(column),Scale(30+row*30),Left(column)+Scale(widths[column]),Scale(60+row*30)};}
static RECT Clipped(RECT a,RECT b){return {(std::max)(a.left,b.left),(std::max)(a.top,b.top),(std::min)(a.right,b.right),(std::min)(a.bottom,b.bottom)};}
static bool Same(RECT a,RECT b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool ListView_GetItemRect(HWND list,int row,RECT* rect,UINT part)
{assert(list==1&&part==LVIR_BOUNDS);if(!validItem)return false;*rect=Cell(row,0);rect->right=Left(7);return true;}
static void ListView_GetSubItemRect(HWND list,int row,int column,UINT part,RECT* rect)
{assert(list==1&&part==LVIR_BOUNDS);*rect=Cell(row,column);if(!column)rect->right=Left(7);}
static HWND ListView_GetHeader(HWND list){assert(list==1);return 2;}
static int Header_GetItemCount(HWND header){assert(header==2);return 7;}
static bool Header_GetItemRect(HWND header,int column,RECT* rect)
{assert(header==2);*rect=Cell(0,column);rect->left+=scrollX;rect->right+=scrollX;return true;}
static void MapWindowPoints(HWND header,HWND list,POINT* points,UINT count)
{assert(header==2&&list==1&&count==2);for(unsigned i=0;i<count;++i)points[i].x-=scrollX;}
static UINT ListView_GetItemState(HWND list,int row,UINT mask){assert(list==1);return states[row]&mask;}
static HANDLE GetProp(HWND list,const wchar_t* name)
{assert(list==1);if(!std::wcscmp(name,L"ReaderLibraryList"))return library?reinterpret_cast<HANDLE>(1):nullptr;assert(!std::wcscmp(name,L"ReaderTitleFont"));return reinterpret_cast<HFONT>(3);}
static void GetClientRect(HWND list,RECT* rect){assert(list==1);*rect={0,0,clientWidth,Scale(200)};}
static int SaveDC(HDC dc){dc->saved.push_back(dc->state);return static_cast<int>(dc->saved.size());}
static void RestoreDC(HDC dc,int saved){assert(saved>0&&saved<=static_cast<int>(dc->saved.size()));dc->state=dc->saved[saved-1];dc->saved.resize(saved-1);}
static void IntersectClipRect(HDC dc,int l,int t,int r,int b){dc->state.clip=Clipped(dc->state.clip,{l,t,r,b});}
static void Fill(HDC dc,RECT rect,COLORREF color)
{rect=Clipped(rect,dc->state.clip);if(rect.left<rect.right&&rect.top<rect.bottom)dc->fills.push_back({rect,color});}
static HGDIOBJ SelectObject(HDC dc,HFONT font){auto old=dc->state.font;dc->state.font=font;return old;}
static int GetWindowDpiCompat(HWND list){assert(list==1);return dpi;}
static bool IsWindowEnabled(HWND list){assert(list==1);return enabled;}
static HWND GetFocus(){return focusWindow;}
static void SetBkMode(HDC,int mode){assert(mode==TRANSPARENT);}
static void SetTextColor(HDC dc,COLORREF color){dc->state.color=color;}
static void InflateRect(RECT* rect,int x,int y){rect->left-=x;rect->right+=x;rect->top-=y;rect->bottom+=y;}
static void DrawFocusRect(HDC dc,RECT* rect){dc->focus.push_back(Clipped(*rect,dc->state.clip));}
static std::wstring Label(int row,int column){return column?L"row"+std::to_wstring(row)+L" / column"+std::to_wstring(column):std::wstring(700,L'书');}
static LRESULT SendMessage(HWND list,UINT message,int row,LPARAM argument)
{
    assert(list==1);if(message==WM_GETFONT)return 2;if(message==WM_QUERYUISTATE)return hideFocus?UISF_HIDEFOCUS:0;
    assert(message==LVM_GETITEMTEXT);auto item=reinterpret_cast<LVITEM*>(argument);auto text=Label(row,item->iSubItem);
    auto length=(std::min)(text.size(),static_cast<size_t>(item->cchTextMax-1));
    std::wmemcpy(item->pszText,text.data(),length);item->pszText[length]=0;return static_cast<LRESULT>(length);
}
static void DrawText(HDC dc,const wchar_t* text,int length,RECT* rect,UINT flags)
{assert(length==-1&&(flags&DT_END_ELLIPSIS));dc->text.push_back({*rect,dc->state.clip,dc->state.color,dc->state.font,text});}
#include "library_row_paint.inc"

static Canvas Paint(int row)
{
    Canvas canvas;NMLVCUSTOMDRAW draw={{&canvas,static_cast<uintptr_t>(row)},0};
    auto result=DrawLibraryRow(1,&draw);assert(result==(CDRF_SKIPDEFAULT|CDRF_SKIPPOSTPAINT));
    assert(canvas.saved.empty()&&canvas.state.font==nullptr&&Same(canvas.state.clip,{0,0,1000000,1000000}));
    return canvas;
}
int main(int argc,char** argv)
{
    assert(argc==2);std::string test=argv[1];palette=MakeReaderPalette(0);
    if(test=="geometry")
    {
        for(int dark:{0,1})for(int resolution:{96,120,144,168,192})for(int offset:{0,150,900})for(int width:{200,700,1600})
        {
            palette=MakeReaderPalette(dark);dpi=resolution;scrollX=Scale(offset);clientWidth=Scale(width);
            auto canvas=Paint(0);auto first=canvas.fills.front();
            assert(Same(first.bounds,{0,Scale(30),clientWidth,Scale(60)})&&first.color==palette.selected);
            assert(canvas.focus.size()==1&&Same(canvas.focus[0],{1,Scale(30)+1,clientWidth-1,Scale(60)-1}));
            assert(canvas.text.size()==7);
            for(int i=0;i<7;++i)
            {
                auto text=canvas.text[i];auto cell=Cell(0,i);int inset=ReaderItemInset(cell.right-cell.left,dpi);
                assert(text.color==palette.selectedText&&text.text==Label(0,i));
                assert(text.bounds.left==cell.left+inset&&text.bounds.right==cell.right-inset);
                assert(text.font==reinterpret_cast<HFONT>(i?2:3));
            }
        }
    }
    else if(test=="states")
    {
        for(int dark:{0,1})
        {
            palette=MakeReaderPalette(dark);auto a=Paint(0),b=Paint(1),c=Paint(2);
            assert(a.focus.size()==1&&b.focus.empty()&&c.focus.empty());
            assert(a.fills[0].color==palette.selected&&b.fills[0].color==palette.selected&&c.fills[0].color==palette.surface);
            assert(c.text[0].color==palette.text&&c.text[1].color==palette.muted);
            assert(states[0]==(LVIS_SELECTED|LVIS_FOCUSED)&&states[1]==LVIS_SELECTED&&states[2]==0);
            states[0]=LVIS_SELECTED;states[2]=LVIS_FOCUSED;assert(Paint(0).focus.empty()&&Paint(2).focus.size()==1);
            states[0]=LVIS_SELECTED|LVIS_FOCUSED;states[2]=0;
        }
        focusWindow=0;assert(Paint(0).focus.empty());focusWindow=1;
        hideFocus=true;assert(Paint(0).focus.empty());hideFocus=false;assert(Paint(0).focus.size()==1);
        // System high-contrast palette values are consumed as-is, including selection text.
        palette.surface=palette.text=ThemeRgb(255,255,0);palette.selected=ThemeRgb(0,0,0);palette.selectedText=ThemeRgb(255,255,255);
        auto high=Paint(0);assert(high.fills[0].color==palette.selected);for(auto text:high.text)assert(text.color==palette.selectedText);
        enabled=false;auto disabled=Paint(0);assert(disabled.focus.empty());for(auto text:disabled.text)assert(text.color==palette.disabled);
    }
    else if(test=="scope")
    {
        Canvas canvas;NMLVCUSTOMDRAW draw={{&canvas,0},0};library=false;
        for(int i=0;i<7;++i){draw.iSubItem=i;DrawPaddedCell(1,&draw);}
        assert(canvas.focus.size()==7); // Existing behavior of other padded lists is unchanged.
        library=true;canvas.focus.clear();DrawPaddedCell(1,&draw);assert(canvas.focus.empty());
        clientWidth=1;assert(Paint(0).focus.empty());clientWidth=0;
        auto empty=Paint(0);assert(empty.focus.empty()&&empty.fills.empty());
        validItem=false;assert(DrawLibraryRow(1,&draw)==CDRF_DODEFAULT);
    }
    else assert(false);
}
