#include "GdiSurface.h"
#include "ThemePalette.h"
#include "ReaderInteraction.h"
#include <climits>
#include <iostream>

static HWND _hReadingView = 2;
static RECT body = {0,0,754,572};
static HDC output = nullptr;
static int paintDpi = 96;
static RECT expectedContent = {28,28,726,544};
static bool SameRect(const RECT& a,const RECT& b) {return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static int copies = 0, pageDraws = 0, prepared = 0, finished = 0, chromePaints = 0, updates = 0;
static int shownChapter = 0, currentChapter = 0, selectedChapter = -1, scroll = 123, reveals = 0, selections = 0;
static BOOL _syncingToc = FALSE;
static int _tocCurrent = -1;
static HWND _hTreeView = 3;
static std::vector<HTREEITEM> _chapterItems = { 10,20,30 };
static std::vector<HWND> invalidated;
struct Settings { int ui_theme = 0, reading_custom = 0, alpha = 255; uint32_t bg_color = 0, font_color = 0; RECT internal_border = {}; };
static Settings settings, *_header = &settings;
struct Loading { bool enable = false; };
static Loading* _loading = nullptr;
static GdiSurface _readingSurface;
static HFONT _readerUIFont = &originalFont;
struct FakeBook
{
    bool loading = false; int index = 401;
    bool IsLoading() const { return loading; }
    int GetCurChapterIndex() const { return currentChapter; }
    void DrawPage(HWND owner, HDC dc, RECT* page, BOOL alpha)
    {
        assert(owner == 1 && !alpha && dc != output);
        assert(SameRect(*page,expectedContent) && SameRect(dc->clip,expectedContent));
        ++pageDraws;
    }
};
static FakeBook book, *_Book = &book;
namespace Gdiplus { struct Bitmap {}; struct Graphics { explicit Graphics(HDC) {} void DrawImage(Bitmap*, int, int, int, int) {} }; }
static ReaderColors ThemeReadingColors(Settings* s) { return ResolveReaderColors(s->ui_theme,s->reading_custom,s->bg_color,s->font_color,s->font_color,false); }
static Gdiplus::Bitmap* LoadBGImage(int,int) { return nullptr; }
static void GetClientRect(HWND child, RECT* rc) { assert(child == _hReadingView); *rc = body; }
static int GetWindowDpiCompat(HWND child) { assert(child == _hReadingView);return paintDpi; }
static int SetBkMode(HDC,int) { return 0; }
static COLORREF SetTextColor(HDC,COLORREF) { return 0; }
static void DrawText(HDC dc,const wchar_t*,int,RECT* rc,int) {assert(SameRect(*rc,expectedContent)&&SameRect(dc->clip,expectedContent));}
const int TRANSPARENT=1, DT_CENTER=2, DT_WORDBREAK=4, SRCCOPY=8, MIN_ALPHA_VALUE=16, LWA_ALPHA=2;
#define _T(s) L##s
static void PrepareReadingJump(HWND owner) { assert(owner == 1); ++prepared; }
static void FinishReadingJump() { ++finished; }
static void DrawLoading(HDC dc,const RECT& rc) {assert(SameRect(rc,expectedContent)&&SameRect(dc->clip,expectedContent));}
static void UpdateProgess() { ++updates; }
static void UpdateTitle(HWND) {}
static void BitBlt(HDC dest,int x,int y,int width,int height,HDC src,int sx,int sy,int)
{
    assert(dest == output && !x && !y && !sx && !sy);
    auto bitmap = static_cast<FakeBitmap*>(src->bitmap);
    assert(width == body.right && height == body.bottom && bitmap->width == width && bitmap->height == height);
    assert(src->saved.empty()&&src->clip.left==0&&src->clip.right==1000000);
    ++copies;
}
static void ThemeSetCurrentChapter(HWND, HTREEITEM item) { assert(_syncingToc); shownChapter=(int)item; }
static void TreeView_SelectItem(HWND, HTREEITEM item) { assert(_syncingToc); selectedChapter=(int)item; ++selections; }
static void TreeView_EnsureVisible(HWND, HTREEITEM item) { assert(_syncingToc); scroll=(int)item; ++reveals; }
static void InvalidateRect(HWND child,void*,BOOL erase) { assert(!erase); invalidated.push_back(child); }
static void SyncCurrentChapter(BOOL reveal = FALSE);

struct SIZE { int cx,cy; };
static BOOL GetTextExtentPoint32(HDC,const wchar_t*,int,SIZE* size) { *size={8,16};return TRUE; }
struct line_info_t { int start=0,length=0,cy=16,gap=0; };
#define DRAW_NULL 0
#define DRAW_PAGE_DOWN 1
#define DRAW_LINE_DOWN 3
#define DRAW_PAGE_UP 2
#define DRAW_LINE_UP 4
#define CHAPTER_PAGE false
#define NO_BLANS_LINE false
#define PART_GAP 0
#define ASSERT assert
// Only font metrics/line formation are substitutes: CalcPageDown below is the
// production function, covering the no-room result of native GDI layout.
class Page
{
public:
    struct { int start=0,length=0; struct { int used=0;line_info_t lines[8]; } lines; } m_PageInfo;
    int m_Index=401,m_PageLength=100,m_DrawType=DRAW_NULL,m_LineCount=0,m_Length=1000,m_ChapterStart=0,m_ChapterLength=0;
    wchar_t m_Text[1000]={};
    int layoutWidth=0,layoutHeight=0;
    void CalcPageDown(HDC,RECT*);
    void CalcPageUp(HDC,RECT*);
    int GetMaxPageLength(HDC,int width,int height) { layoutWidth=width;layoutHeight=height;return 64; }
    void ClearLines() { m_PageInfo.lines.used=0; }
    void RemoveLines(int,int) { m_PageInfo.lines.used=0; }
    int GetTextBeginIndex() { return 0; }
    bool GetChapterInfo(int,int*,int*) { return false; }
    int GetNextParagraph(int start,int,int* blank,int* crlf) { *blank=0;*crlf=0;return start<m_Length?80:0; }
    int GetPrevParagraph(int,int,int*,int*) { return 0; }
    void SelectFont(HDC,int,BOOL) {}
    void AddCharsToLine(int,void*,int,int,int,int,int,int,int) {}
    int ParagraphToLines(HDC,int,int,int,int,int) { return 0; }
};
#include "reader_paint.inc"

static void Reuse()
{
    GdiSurface surface;
    int created = createdDIBs;
    assert(surface.Ensure(nullptr, 100, 50)); auto pixels = surface.Pixels(); auto dc = surface.DC();
    for (int i=0;i<200;++i) assert(surface.Ensure(nullptr,100,50));
    assert(surface.DC()==dc && surface.Pixels()==pixels && createdDIBs==created+1);
    assert(!surface.Ensure(nullptr,0,1) && surface.DC()==dc);
    failDIB=true; assert(!surface.Ensure(nullptr,120,60)); failDIB=false;
    assert(surface.Pixels()==pixels && surface.Ensure(nullptr,100,50));
    assert(surface.Ensure(nullptr,120,60) && createdDIBs==created+2);
    surface.Reset(); surface.Reset();
}
static void Paint()
{
    output=CreateCompatibleDC(nullptr);
    for (int i=0;i<200;++i) OnPaint(1,output);
    assert(copies==200 && pageDraws==200 && prepared==200 && finished==200);
    assert(book.index==401 && !chromePaints && createdDIBs==1);
    body={0,0,550,600}; expectedContent={28,28,522,572};
    OnPaint(1,output); assert(book.index==401 && createdDIBs==2);
    settings.internal_border={60,5,44,72}; expectedContent={60,28,506,528};
    OnPaint(1,output);assert(book.index==401&&createdDIBs==2);
    paintDpi=144;expectedContent={60,42,506,528};OnPaint(1,output);
    paintDpi=96;settings.internal_border={};expectedContent={28,28,522,572};
    auto drawn=pageDraws;
    _Book=nullptr; OnPaint(1,output); assert(pageDraws==drawn);
    book.loading=true; _Book=&book; OnPaint(1,output); assert(pageDraws==drawn);
    book.loading=false;body={0,0,1,1};expectedContent=body;OnPaint(1,output);assert(book.index==401);
    auto copied=copies;body={0,0,0,0}; OnPaint(1,output); assert(copies==copied);
    _readingSurface.Reset(); DeleteDC(output); output=nullptr;
}
static void Dirty()
{
    for (int i=0;i<200;++i) Invalidate(1,TRUE,TRUE);
    assert(invalidated.size()==200 && std::all_of(invalidated.begin(),invalidated.end(),[](HWND w){return w==_hReadingView;}));
    Invalidate(1,FALSE,FALSE); assert(invalidated.size()==202 && invalidated.back()==1);
    auto count=invalidated.size();Invalidate(1,TRUE,FALSE);
    assert(invalidated.size()==count+1&&invalidated.back()==_hReadingView);

}
static void Sync()
{
    SyncCurrentChapter(); assert(shownChapter==10 && !selections && !reveals && scroll==123);
    currentChapter=2; SyncCurrentChapter(); assert(shownChapter==30 && !selections && scroll==123);
    for (int i=0;i<200;++i) SyncCurrentChapter();
    assert(!selections && !reveals && scroll==123);
    SyncCurrentChapter(TRUE); assert(selectedChapter==30 && selections==1 && reveals==1 && scroll==30 && !_syncingToc);
    book.loading=true; currentChapter=1; SyncCurrentChapter(TRUE); assert(selections==1);
    book.loading=false; currentChapter=-1; SyncCurrentChapter(TRUE); assert(selections==1);
}
static void EmptyReflow()
{
    Page page; RECT bounds={0,0,1,1};
    for(int i=0;i<200;++i)
    {
        page.m_DrawType=DRAW_NULL; page.m_PageLength=100;
        page.CalcPageDown(nullptr,&bounds);
        assert(page.m_PageInfo.start==401 && page.m_PageInfo.length==0);
        page.m_Index=page.m_PageInfo.start;
    }
    page.m_PageLength=100; page.m_DrawType=DRAW_PAGE_DOWN;
    page.CalcPageDown(nullptr,&bounds); assert(page.m_PageInfo.start==501);
    page.m_DrawType=DRAW_LINE_DOWN;
    page.CalcPageDown(nullptr,&bounds); assert(page.m_PageInfo.start==501);
}
static void InsetPagination()
{
    Page page;RECT bounds={28,28,726,544};
    page.CalcPageDown(nullptr,&bounds);
    assert(page.layoutWidth==698&&page.layoutHeight==516&&page.m_PageInfo.start==401);
    page.m_DrawType=DRAW_PAGE_UP;page.CalcPageUp(nullptr,&bounds);
    assert(page.layoutWidth==698&&page.layoutHeight==516&&page.m_PageInfo.start==401);
    bounds={60,42,710,500};page.m_DrawType=DRAW_NULL;page.CalcPageDown(nullptr,&bounds);
    assert(page.layoutWidth==650&&page.layoutHeight==458&&page.m_PageInfo.start==401);
}
int main(int argc,char** argv)
{
    assert(argc==2); std::string test=argv[1];
    if(test=="reuse") Reuse(); else if(test=="paint") Paint(); else if(test=="dirty") Dirty(); else if(test=="sync") Sync(); else if(test=="reflow") EmptyReflow(); else if(test=="insets") InsetPagination(); else return 2;
    assert(createdDCs==deletedDCs && createdDIBs==deletedDIBs && liveDCs.empty());
    std::cout<<test<<" passed\n";
}
