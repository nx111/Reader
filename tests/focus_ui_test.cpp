#include "ReaderInteraction.h"
#include "NavigationLayout.h"
#include "ThemePalette.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>

namespace Bridge {
using BOOL=int;using HWND=void*;using WPARAM=unsigned;using LPARAM=long;
struct MSG {HWND hwnd;unsigned message;WPARAM wParam;LPARAM lParam;};
enum {FALSE=0,TRUE=1,VK_ESCAPE=27,VK_CONTROL=17,VK_MENU=18,WM_KEYDOWN=256,WM_KEYUP=257,SW_HIDE=0,SW_SHOWNA=8};
static HWND _hWnd=(HWND)1,_hTreeMark=(HWND)3;
static BOOL _focusMode=FALSE,_panelFromFocus=FALSE,_tocVisible=FALSE,_navigationAccess=FALSE,_focusEscapeLatched=FALSE;
static ReaderFocusPanels _focusPanels;
static bool marks=false,modal=false,editing=false,navigation=false,menu=false,control=false,alt=false,escapeHeld=true;
static int cancelled=0,endedEdit=0,closedPanel=0,leftNavigation=0,layouts=0,dirty=0,anchor=55;
struct Drag {bool value=false;bool Active() {return value;}} _splitterDrag;
BOOL IsWindowVisible(HWND) {return marks;}void ShowWindow(HWND,int show) {marks=show!=SW_HIDE;}
BOOL IsChild(HWND,HWND window) {return window==(HWND)2;}
BOOL ReaderNavigationContains(HWND window) {return window==(HWND)4;}
int GetKeyState(int key) {return (key==VK_CONTROL?control:alt)?0x8000:0;}
int GetAsyncKeyState(int) {return escapeHeld?0x8000:0;}
BOOL IsWindowEnabled(HWND) {return !modal;}BOOL EC_IsEditMode() {return editing;}
BOOL ReaderNavigationKeyboardActive() {return navigation;}BOOL ReaderNavigationTracking() {return menu;}
void CancelSplitter(HWND) {++cancelled;_splitterDrag.value=false;}
void EC_LeaveEditMode() {++endedEdit;editing=false;}
void CloseToc(HWND) {++closedPanel;_panelFromFocus=FALSE;_focusMode=TRUE;}
void LeaveReaderNavigation() {++leftNavigation;navigation=false;}
void UpdateReaderLayout(HWND) {++layouts;}void Invalidate(HWND,BOOL,BOOL) {++dirty;}void SetFocus(HWND) {}
#include "reader_focus.inc"
void KeyUp() {MSG key={_hWnd,WM_KEYUP,VK_ESCAPE,0};ReaderFocusEscape(&key);}
void EscapeRegression()
{
    _tocVisible=TRUE;SetReaderFocusMode(TRUE);assert(_focusMode&&_focusPanels.toc&&!marks);
    MSG key={_hWnd,WM_KEYDOWN,VK_ESCAPE,0};
    modal=true;assert(!ReaderFocusEscape(&key)&&_focusMode&&!_focusEscapeLatched);modal=false;
    menu=true;assert(!ReaderFocusEscape(&key)&&_focusMode);menu=false;
    editing=true;assert(ReaderFocusEscape(&key)&&endedEdit==1&&_focusMode);
    assert(ReaderFocusEscape(&key)&&endedEdit==1);KeyUp();
    _splitterDrag.value=true;assert(ReaderFocusEscape(&key)&&cancelled==1&&_focusMode);KeyUp();
    navigation=true;assert(ReaderFocusEscape(&key)&&leftNavigation==1&&_focusMode);KeyUp();
    control=true;assert(!ReaderFocusEscape(&key)&&_focusMode);control=false;
    assert(ReaderFocusEscape(&key)&&!_focusMode&&_tocVisible&&layouts==1&&dirty==1&&anchor==55);
    assert(ReaderFocusEscape(&key)&&layouts==1);KeyUp();
    assert(!ReaderFocusEscape(&key)); // ordinary-mode Esc remains owned by existing commands
    editing=true;escapeHeld=false;assert(ReaderFocusEscape(&key)&&endedEdit==2&&!_focusMode&&!_focusEscapeLatched);escapeHeld=true;KeyUp();
    _splitterDrag.value=true;assert(ReaderFocusEscape(&key)&&cancelled==2&&!_focusMode);KeyUp();
    SetReaderFocusMode(TRUE);_panelFromFocus=TRUE;_focusMode=FALSE;
    assert(ReaderFocusEscape(&key)&&closedPanel==1&&_focusMode);KeyUp();
    assert(ReaderFocusEscape(&key)&&!_focusMode);KeyUp();
    key.hwnd=(HWND)9;SetReaderFocusMode(TRUE);assert(!ReaderFocusEscape(&key)&&_focusMode);
    key.hwnd=(HWND)4;assert(ReaderFocusEscape(&key)&&!_focusMode);KeyUp();
}
void RestoreRegression()
{
    for(int i=0;i<200;++i)
    {
        _focusMode=FALSE;_panelFromFocus=FALSE;_focusPanels.Clear();_tocVisible=i%2;marks=!_tocVisible;
        bool beforeToc=_tocVisible,beforeMarks=marks;
        SetReaderFocusMode(TRUE);assert(_focusMode&&!marks);SetReaderFocusMode(TRUE);
        SetReaderFocusMode(FALSE);assert(!_focusMode&&_tocVisible==beforeToc&&marks==beforeMarks&&anchor==55);
    }
}
}
double Luminance(uint32_t color)
{
    double result=0;double weights[]={0.2126,0.7152,0.0722};
    for(int i=0;i<3;++i) {double n=((color>>(8*i))&255)/255.0;result+=weights[i]*(n<=0.04045?n/12.92:pow((n+0.055)/1.055,2.4));}return result;
}
double Contrast(uint32_t a,uint32_t b)
{double x=Luminance(a),y=Luminance(b);return ((std::max)(x,y)+0.05)/((std::min)(x,y)+0.05);}
int main(int argc,char** argv)
{
    assert(argc==2);std::string action=argv[1];
    if(action=="escape") Bridge::EscapeRegression();
    else if(action=="restore") Bridge::RestoreRegression();
    else if(action=="geometry")
        for(int dpi:{96,120,144,168,192}) for(int width:{240,300,700,1000,1800})
        {
            auto g=ComputeReadingGeometry(ReaderScale(width,dpi),ReaderScale(500,dpi),dpi,true,false,240,true);
            auto nav=ReaderNavigationLayout(ReaderScale(width,dpi),ReaderScale(500,dpi),dpi,true,false,true);
            assert(g.navigationWidth==ReaderScale(48,dpi)&&g.readingLeft==nav.width&&g.readingTop==0&&nav.compact);
            assert(nav.headerHeight==0&&nav.viewportHeight+nav.footerHeight==ReaderScale(500,dpi));
            auto normal=ReaderNavigationLayout(ReaderScale(width,dpi),ReaderScale(500,dpi),dpi,true,false);
            assert(normal.headerHeight==ReaderScale(44,dpi));
            if(normal.compact) assert(nav.viewportHeight==normal.viewportHeight+normal.headerHeight&&nav.footerTop==normal.footerTop);
            for(int height:{0,20,76,120,280})
            {
                auto small=ReaderNavigationLayout(ReaderScale(width,dpi),ReaderScale(height,dpi),dpi,true,false,true);
                assert(small.headerHeight==0&&small.viewportHeight+small.footerHeight==ReaderScale(height,dpi));
                assert(small.scrollMaximum==(std::max)(0,small.contentHeight-small.viewportHeight));
            }
            auto repeated=ComputeReadingGeometry(ReaderScale(width,dpi),ReaderScale(500,dpi),dpi,true,false,240,true);
            assert(g.readingLeft==repeated.readingLeft&&g.readingBottom==repeated.readingBottom);
        }
    else if(action=="emphasis")
        for(int mask=0;mask<64;++mask)
        {
            bool active=mask&1,pointer=mask&2,focus=mask&4,menu=mask&8,capture=mask&16;
            assert(ReaderRailClear(true,active,pointer,focus,menu,capture)==(active&&(pointer||focus||menu||capture)));
            assert(ReaderRailClear(false,active,pointer,focus,menu,capture));
        }
    else if(action=="palette")
        for(int dark:{0,1})
        {
            auto p=MakeReaderPalette(dark);auto idle=ReaderScrollbarColors(p,p.surface);
            assert(idle.background==p.surface&&idle.thumb!=p.text&&idle.thumb!=idle.background);
            assert(Contrast(idle.thumb,idle.background)<3&&Contrast(idle.hot,idle.background)<4);
            assert(ReaderBlendColor(p.text,p.window,255)==p.text&&ReaderBlendColor(p.text,p.window,0)==p.window);
        }
    else if(action=="padding")
        for(int dpi:{96,120,144,168,192})
        {
            assert(ReaderItemPadding(dpi)==8*dpi/96);
            for(int width:{0,1,2,5,10,20,65,200}) {int pad=ReaderItemInset(width,dpi);assert(pad>=0&&pad<=ReaderItemPadding(dpi));if(width)assert(width-2*pad>=1);}
        }
    else assert(false);
}
