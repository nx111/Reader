"""Portable model and production bridge checks, not Windows desktop/UIA pixels."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class ScrollbarUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='reader-scrollbars-');cls.addClassCleanup(cls.temp.cleanup)
        cls.work=Path(cls.temp.name)
        nav=(ROOT/'Reader/NavigationPane.cpp').read_text()
        refresh='void RefreshEmphasis('+nav.split('void RefreshEmphasis(',1)[1].split('void Position(',1)[0]
        layout='void LayoutReaderNavigation('+nav.split('void LayoutReaderNavigation(',1)[1].split('void ReaderNavigationMenuChanged()',1)[0]
        theme=(ROOT/'Reader/Theme.cpp').read_text()
        colors='ReaderPalette ThemeControlPalette('+theme.split('ReaderPalette ThemeControlPalette(',1)[1].split('HBRUSH ThemeWindowBrush()',1)[0]
        (cls.work/'navigation_paint.inc').write_text(refresh+layout+colors)
        cls.flags=[*shlex.split(os.environ.get('CXX','g++')),'-std=c++14','-Wall','-Wextra',*shlex.split(os.environ.get('READER_LIBRARY_TEST_FLAGS','')),'-I'+str(cls.work),'-I'+str(ROOT/'Reader'),'-I'+str(ROOT/'tests')]

    def execute(self,name,source):
        cpp=self.work/(name+'.cpp');exe=cpp.with_suffix('');cpp.write_text(source)
        result=subprocess.run([*self.flags,str(cpp),'-o',str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)
        result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode,0,result.stdout)

    def test_range_and_native_thumb_include_32bit_extremes_and_rtl_geometry(self):
        self.execute('range',r'''
#include "ScrollbarModel.h"
#include <cassert>
#include <climits>
int main() {
 assert(!ReaderScrollHasRange(0,0,0)&&!ReaderScrollHasRange(0,99,100));
 assert(!ReaderScrollHasRange(4,3,0)&&ReaderScrollHasRange(0,100,100));
 assert(ReaderScrollMaximum(INT_MIN,INT_MAX,0)==INT_MAX);
 assert(ReaderScrollMaximum(INT_MIN,INT_MAX,UINT_MAX)==INT_MIN+1LL);
 assert(ReaderScrollMaximum(0,2000000,500)==1999501);
 for(int dpi:{96,120,144,168,192})for(int available:{0,3,17,34})assert(ReaderScrollbarThickness(available,dpi)<=available);
 for(int length:{0,1,20,400,1000})for(int start:{-20,0,10,2000}) {
  auto t=ReaderScrollbarNativeThumb(length,start,start+50);assert(t.start>=0&&t.end<=length&&t.start<=t.end);
  if(t.end>t.start){assert(t.Contains(t.start)&&!t.Contains(t.end));}
 }
}
''')

    def test_hover_capture_delayed_fade_reentry_reset_and_repeated_drag(self):
        self.execute('visibility',r'''
#include "ScrollbarModel.h"
#include <cassert>
int main() {
 ReaderScrollbarVisibility v;assert(!v.opacity&&!v.Animate());
 for(int n=0;n<200;++n) {
  v.Pointer(true,false,1000);assert(v.opacity==255&&!v.Animate()&&!v.thumb);
  v.Pointer(true,true,1001);assert(v.thumb);
  v.Drag(true,1002);v.Pointer(false,false,1003);v.Tick(9999);assert(v.opacity==255&&!v.Animate());
  v.Drag(false,10000);assert(v.Animate());v.Tick(10250);assert(v.opacity==255);
  v.Tick(10340);assert(v.opacity>0&&v.opacity<255);
  v.Pointer(true,true,10341);assert(v.opacity==255&&!v.Animate());
  v.Pointer(false,false,10342);v.Tick(11000);assert(!v.opacity&&!v.Animate());
  v.Drag(true,11001);v.Reset();assert(!v.opacity&&!v.pointer&&!v.thumb&&!v.drag&&!v.Animate());
 }
}
''')

    def test_light_dark_custom_background_scroll_colors_are_neutral_and_gradual(self):
        self.execute('colors',r'''
#include "ThemePalette.h"
#include <cassert>
#include <cstdlib>
int distance(uint32_t a,uint32_t b) {int d=0;for(int shift:{0,8,16})d+=abs(int((a>>shift)&255)-int((b>>shift)&255));return d;}
int main() {
 for(int dark:{0,1})for(uint32_t bg:{0u,0xffffffu,0x222222u,0xf6f6f6u,0xddddc9u}) {
  auto p=MakeReaderPalette(dark);auto c=ReaderScrollbarColors(p,bg);assert(c.background==bg);
  assert(distance(c.track,bg)<distance(c.thumb,bg)&&distance(c.thumb,bg)<distance(c.hot,bg)&&distance(c.hot,bg)<distance(c.pressed,bg));
  assert(c.pressed!=0&&c.pressed!=0xffffff&&ReaderBlendColor(c.thumb,bg,0)==bg);
 }
}
''')

    def test_native_tracking_forward_preserves_capture_loop_full_position_and_destroy(self):
        source=(ROOT/'Reader/Scrollbar.cpp').read_text().split('LRESULT Track(',1)[1].split('LRESULT CALLBACK StripProc',1)[0]
        self.execute('track',r'''
#include <cassert>
#include <initializer_list>
using HWND=int;using UINT=unsigned;using WPARAM=unsigned;using LPARAM=long;using LRESULT=long;
struct POINT{int x,y;};enum{SB_VERT=1,SB_HORZ=0,HTVSCROLL=7,HTHSCROLL=6,WM_NCLBUTTONDOWN=161};
#define GET_X_LPARAM(p) int((p)&65535)
#define GET_Y_LPARAM(p) int(((p)>>16)&65535)
#define MAKELPARAM(x,y) ((x)|((y)<<16))
struct Host{HWND target=4;bool alive=true;};struct Strip{Host* host;int window=2,axis=1;bool range=true,pinned=false;};
int pointers=0,native=0,trackPosition=0;bool destroy=false;Host* current=nullptr;
void Pointer(Strip& s){++pointers;if(s.pinned)assert(s.host->alive);}
void UpdateWindow(HWND){}void ClientToScreen(HWND,POINT* p){p->x+=10;p->y+=20;}
LRESULT SendMessage(HWND target,UINT message,WPARAM hit,LPARAM point){assert(target==4&&message==WM_NCLBUTTONDOWN&&(hit==HTVSCROLL||hit==HTHSCROLL));assert(GET_X_LPARAM(point)==15&&GET_Y_LPARAM(point)==26);++native;trackPosition=1000000;if(destroy)current->alive=false;return 9;}
LRESULT Track('''+source+r'''
int main(){Host h;current=&h;Strip s{&h};for(int axis:{SB_HORZ,SB_VERT}){s.axis=axis;assert(Track(s,WM_NCLBUTTONDOWN,0,MAKELPARAM(5,6))==9&&!s.pinned&&trackPosition==1000000);}assert(native==2&&pointers==4);destroy=true;assert(Track(s,WM_NCLBUTTONDOWN,0,MAKELPARAM(5,6))==9&&!h.alive&&pointers==5);s.range=false;assert(!Track(s,WM_NCLBUTTONDOWN,0,0)&&native==3);}
'''.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>'))

    def test_ctrl_wheel_fractional_direction_bounds_release_capture_and_book_switch(self):
        self.execute('wheel',r'''
#include "ReaderInteraction.h"
#include <cassert>
#include <climits>
int main(){ReaderCtrlWheel w;
 assert(!w.Feed(30,true,true,false,1));assert(!w.Feed(30,true,true,false,1));assert(!w.Feed(30,true,true,false,1));assert(w.Feed(30,true,true,false,1)==1);
 assert(w.Feed(-240,true,true,false,1)==-2);assert(!w.Feed(60,true,true,false,1));assert(!w.Feed(-60,true,true,false,1));
 assert(!w.Feed(60,true,true,false,1));w.Reset();assert(!w.Feed(60,true,true,false,1));
 assert(!w.Feed(60,false,true,false,1));assert(!w.Feed(60,true,true,false,1));
 assert(!w.Feed(60,true,false,false,1));assert(!w.Feed(60,true,true,false,1));
 assert(!w.Feed(60,true,true,true,1));assert(!w.Feed(60,true,true,false,1));
 assert(!w.Feed(60,true,true,false,2));assert(w.Feed(60,true,true,false,2)==1);
 assert(ReaderFontPoints(72,INT_MAX)==72&&ReaderFontPoints(1,INT_MIN)==1);
 for(int p=2;p<72;++p)assert(ReaderFontPoints(ReaderFontPoints(p,1),-1)==p);
}
''')

    def test_actual_font_zoom_preserves_anchor_point_bounds_and_dpi(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text()
        font=reader.split('static void ZoomReaderText(HWND hWnd,int steps)\n',1)[1].split('LRESULT OnFontZoomIn',1)[0]
        self.execute('font',r'''
#include "ReaderInteraction.h"
#include <cassert>
#include <cstdlib>
using HWND=int;using BYTE=unsigned char;const int MIN_ALPHA_VALUE=100;
struct {struct{int lfHeight=-24;}font;int alpha=123;}settings,*_header=&settings;
bool _focusMode=false,_panelFromFocus=false,edit=false;int dpi=96,saved=0;
struct Book{bool loading=false;int index=123456,reset=0,draw=0;bool IsLoading(){return loading;}void ResetLayout(){++reset;}void ReDraw(HWND){++draw;}}book,*_Book=&book;
bool EC_IsEditMode(){return edit;}int GetWindowDpiCompat(HWND){return dpi;}int MulDiv(int a,int b,int c){long long v=(long long)a*b;return int(v<0?-((-v+c/2)/c):(v+c/2)/c);}
void Save(HWND){++saved;}
static void ZoomReaderText(HWND hWnd,int steps)
'''+font+r'''
int main(){for(int scale:{96,120,144,168,192}){dpi=scale;_header->font.lfHeight=-MulDiv(24,dpi,72);for(int i=0;i<200;++i){ZoomReaderText(1,1);ZoomReaderText(1,-1);assert(book.index==123456);}ZoomReaderText(1,10000);assert(abs(_header->font.lfHeight)==MulDiv(72,dpi,72));ZoomReaderText(1,-10000);assert(abs(_header->font.lfHeight)==MulDiv(1,dpi,72));}int before=saved;edit=true;ZoomReaderText(1,1);assert(saved==before);edit=false;book.loading=true;ZoomReaderText(1,1);assert(saved==before);}
''')

    def test_actual_child_rail_layout_focus_minimize_and_repeated_restore(self):
        self.execute('rail',r'''
#include "navigation_paint_compat.h"
#include "navigation_paint.inc"
int main(){InitRail();
 for(int scale:{96,120,144,168,192})for(int i=0;i<40;++i){dpi=scale;
  LayoutReaderNavigation(1000*dpi/96,500*dpi/96,TRUE,FALSE,TRUE);
  assert(geometry.width==48*dpi/96&&geometry.headerHeight==0&&windows[pane].parent==owner&&anchor==12345);
  focus=buttons[5];RefreshEmphasis();assert(GetProp(pane,L"ReaderRailClear"));
  focus=nullptr;foreground=nullptr;windows[owner].iconic=true;RefreshEmphasis();assert(!GetProp(pane,L"ReaderRailClear")&&!IsWindowVisible(pane));
  windows[owner].iconic=false;foreground=owner;RefreshEmphasis();assert(IsWindowVisible(pane)&&windows[pane].parent==owner);
  LayoutReaderNavigation(1000*dpi/96,500*dpi/96,TRUE,FALSE,FALSE);
  assert(geometry.width==176*dpi/96&&geometry.headerHeight==44*dpi/96&&windows[pane].parent==owner&&anchor==12345);
  for(HWND dirty:dirties)assert(dirty!=owner);dirties.clear();
 }
}
''')

    def test_actual_focus_palette_background_hover_custom_colors_and_high_contrast(self):
        self.execute('rail-colors',r'''
#include "navigation_paint_compat.h"
#include "navigation_paint.inc"
int main(){InitRail();
 for(int dark:{0,1})for(uint32_t paper:{0u,0xffffffu,0x777777u,0x222222u,0xddddc9u}){
  palette=MakeReaderPalette(dark);reading.background=paper;
  LayoutReaderNavigation(1000,500,TRUE,FALSE,TRUE);focus=nullptr;foreground=nullptr;RefreshEmphasis();
  auto idle=ThemeControlPalette(buttons[5]);assert(idle.window==paper&&idle.surface==paper&&idle.raised==paper);
  assert(ReaderColorContrast(idle.text,paper)>=2.99&&idle.text!=paper);
  foreground=owner;focus=buttons[5];RefreshEmphasis();auto clear=ThemeControlPalette(buttons[5]);
  assert(clear.window==paper&&ReaderColorContrast(clear.text,paper)>=3.99);
  assert(ReaderColorContrast(clear.text,paper)>=ReaderColorContrast(idle.text,paper));
  highContrast=true;auto system=ThemeControlPalette(buttons[5]);assert(system.window==GetSysColor(COLOR_WINDOW)&&system.text==GetSysColor(COLOR_WINDOWTEXT)&&system.selected==GetSysColor(COLOR_HIGHLIGHT));highContrast=false;
  LayoutReaderNavigation(1000,500,TRUE,FALSE,FALSE);auto normal=ThemeControlPalette(buttons[5]);assert(normal.window==palette.window&&normal.text==palette.text);
 }
}
''')

    def test_scroll_strip_preserves_native_accessibility_scope_and_local_paint(self):
        scroll=(ROOT/'Reader/Scrollbar.cpp').read_text();rail=(ROOT/'Reader/NavigationPane.cpp').read_text()
        for forbidden in ('ShowScrollBar','SetWindowLongPtr(window,GWL_STYLE','WM_NCCALCSIZE','InvalidateRect(GetParent','RDW_ALLCHILDREN','WS_EX_TRANSPARENT'):
            self.assertNotIn(forbidden,scroll)
        for required in ('GetScrollBarInfo','GetScrollInfo','STATE_SYSTEM_INVISIBLE','AccessibleObjectFromWindow','LresultFromObject','WM_MOUSEWHEEL','WM_NCDESTROY','KillTimer','WM_CANCELMODE','HighContrast','std::shared_ptr<Host> keep'):
            self.assertIn(required,scroll)
        for forbidden in ('WS_EX_LAYERED','WS_EX_TOOLWINDOW','SetParent(','SetWindowRgn','CreateRectRgn','ReaderFocusRailHost'):
            self.assertNotIn(forbidden,rail)
        self.assertFalse((ROOT/'Reader/FocusRail.cpp').exists());self.assertFalse((ROOT/'Reader/FocusRail.h').exists())
        for project in ('Reader.vcxproj','Reader.vcxproj.filters'):
            self.assertNotIn('FocusRail.',(ROOT/'Reader'/project).read_text())
        self.assertIn('ReaderScrollbarsUpdate(viewport)',(ROOT/'Reader/PdfBook.cpp').read_text())
        update=(ROOT/'Reader/PdfBook.cpp').read_text().split('void PdfBook::UpdateScrollbars(HWND viewport)',1)[1]
        self.assertIn('SIF_DISABLENOSCROLL',update);self.assertNotIn('ShowScrollBar',update)
        self.assertIn('ReaderScrollbarsUpdate(viewport)',(ROOT/'Reader/NavigationPane.cpp').read_text())

    def test_wheel_scope_consumes_zoom_before_pdf_pan_and_text_scroll(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text();nav=(ROOT/'Reader/NavigationPane.cpp').read_text()
        wheel=reader.split('            if (!PtInRect(&_readingRect, wheel)',1)[1].split('    case WM_ERASEBKGND:',1)[0]
        self.assertLess(wheel.index('_ctrlWheel.Feed'),wheel.index('OnLineUp'))
        self.assertLess(wheel.index('_ctrlWheel.Feed'),wheel.index('->Scroll(hWnd'))
        self.assertIn('readingPointer&&IsWindowEnabled(hWnd)',wheel)
        self.assertIn('WindowFromPoint(wheel)==_hReadingView',reader)
        self.assertIn('GetCapture()!=nullptr',wheel);self.assertIn('EC_IsEditMode()',wheel)
        self.assertNotIn('_header->alpha =',wheel)
        self.assertIn('else ZoomReaderText(hWnd,steps)',wheel);self.assertIn('if(steps)',wheel)
        self.assertIn('message->wParam==VK_TAB && ReaderNavigationContains',nav)
        self.assertIn('!ReaderNavigationContains(message->hwnd)',nav)
        self.assertNotIn('FollowReaderNavigation',reader)
        self.assertNotIn('SetWindowRgn',reader)
        layout=reader.split('static void UpdateReaderLayout(HWND hWnd)\n',1)[1].split('static void SyncCurrentChapter',1)[0]
        self.assertIn('ApplyReaderOpaqueWindow(hWnd)',layout)
        self.assertNotIn('_WndInfo.bLayered',layout)
