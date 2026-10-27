#include "NavigationLayout.h"
#include "ReaderIcons.h"
#include "ThemePalette.h"
#include "PdfLayout.h"
#include "ReaderInteraction.h"
#include "BookmarkExport.h"
#include <cassert>
#include <string>
#include <limits>

int main(int argc,char** argv)
{
    assert(argc==2);std::string test=argv[1];
    if(test=="layout")
        for(int dpi:{96,120,144,168,192}) for(int logical:{240,300,699,700,1000,1600}) for(bool panel:{false,true})
        {
            int w=ReaderScale(logical,dpi),h=ReaderScale(500,dpi);
            auto g=ComputeReadingGeometry(w,h,dpi,true,panel);
            assert(g.readingTop==0&&g.readingBottom==h&&g.readingRight==w);
            assert(g.navigationWidth==ReaderScale(panel||logical<700?48:176,dpi));
            assert(g.readingLeft==g.navigationWidth+(panel&&!g.drawer?g.sidebarWidth+g.splitterWidth:0));
            if(panel&&!g.drawer) assert(w-g.readingLeft>=ReaderScale(320,dpi));
            if(panel&&g.drawer) assert(g.sidebarWidth<=w-g.navigationWidth-ReaderScale(40,dpi));
            auto focus=ComputeReadingGeometry(w,h,dpi,false,false);assert(focus.readingLeft==0&&!focus.readingTop&&!focus.navigationWidth);
        }
    else if(test=="scroll")
        for(int dpi:{96,120,144,192}) for(int h:{280,320,600,900})
        {
            auto g=ReaderNavigationLayout(ReaderScale(300,dpi),ReaderScale(h,dpi),dpi,true,false);
            assert(g.compact&&g.width==ReaderScale(48,dpi));
            assert(g.headerHeight+g.viewportHeight+g.footerHeight==ReaderScale(h,dpi));
            assert(g.footerHeight==ReaderScale(76,dpi));
            assert(g.scrollMaximum==(std::max)(0,g.contentHeight-g.viewportHeight));
            assert(g.contentHeight-g.scrollMaximum<=g.viewportHeight);
        }
    else if(test=="library")
        for(int dpi:{96,120,144,192}) for(int w:{192,252,330,500,820,1400})
        {
            auto g=ReaderLibraryLayout(ReaderScale(w,dpi),ReaderScale(500,dpi),dpi,false);
            assert(!g.sidebar&&g.sidebarWidth==0);assert(g.actionColumns>=1&&g.actionColumns<=9);
            assert((g.actionColumns-1)*(g.actionSize+g.actionGap)+g.actionSize<=g.width);
            assert(g.searchWidth+g.filterWidth<=g.width&&g.listTop>g.actionsTop+g.actionSize);
            assert(g.left+g.width<=ReaderScale(w,dpi)&&g.listHeight>=0);
        }
    else if(test=="keys")
    {
        assert(ReaderNavigationKey(0x79,false,false,false,false)==navigation_key_toggle);
        assert(ReaderNavigationKey(0x79,false,false,true,true)==navigation_key_none);
        assert(ReaderNavigationKey(0x79,false,true,false,true)==navigation_key_none);
        assert(ReaderNavigationKey(0x26,false,false,false,true)==navigation_key_previous);
        assert(ReaderNavigationKey(0x28,false,false,false,true)==navigation_key_next);
        assert(ReaderNavigationKey(0x0d,false,false,false,true)==navigation_key_activate);
        assert(ReaderNavigationKey(0x20,false,false,false,true)==navigation_key_activate);
        assert(ReaderNavigationKey(0x1b,false,false,false,true)==navigation_key_leave);
        assert(ReaderNavigationKey(0x26,false,false,false,false)==navigation_key_none);
        assert(ReaderNavigationKey(0x25,true,false,false,true)==navigation_key_none);
        assert(ReaderNavigationCycle(9,0,-1)==8&&ReaderNavigationCycle(9,8,1)==0);
        ReaderAltNavigationGesture alt;alt.Down(0x12,false,false);assert(alt.Up(0x12));
        alt.Down(0x12,true,false);assert(!alt.Up(0x12));
        alt.Down(0x12,false,false);alt.Down('F',false,false);assert(!alt.Up(0x12));
        alt.Down(0x12,false,false);alt.Cancel();assert(!alt.Up(0x12));
        assert(ReaderMenuMnemonic(L"更多(&M)")==L'M'&&ReaderMenuMnemonic(L"&&&File")==L'F');
    }
    else if(test=="icons")
        for(int i=1;i<=21;++i)
        {
            auto strokes=ReaderIconStrokes((ReaderIcon)i);assert(!strokes.empty());
            for(auto& s:strokes) assert(s.x1>=0&&s.x1<=24&&s.y1>=0&&s.y1<=24&&s.x2>=0&&s.x2<=24&&s.y2>=0&&s.y2<=24);
        }
    else if(test=="colors")
        for(int dark:{0,1})
        {
            auto p=MakeReaderPalette(dark);auto selected=ReaderFlatControlColors(p,true,false,false,true);
            assert(selected.background==p.selected&&selected.ink==p.selectedText);
            assert(ReaderFlatControlColors(p,true,true,false).background==p.hover);
            assert(ReaderFlatControlColors(p,true,true,true).background==p.pressed);
            assert(ReaderFlatControlColors(p,false,false,false).ink==p.disabled);
            assert(p.surface!=p.text&&p.window!=p.selected&&p.hover!=p.pressed);
        }
    else if(test=="reading_insets")
    {
        struct Rect {int left,top,right,bottom;};
        const int dpis[]={96,120,144,168,192},expected[]={28,36,42,50,56};
        for(int i=0;i<5;++i)
        {
            int dpi=dpis[i],pad=ReaderReadingPadding(dpi);
            assert(pad==expected[i]&&pad==2*ReaderSidebarPadding(dpi));
            auto content=ReaderReadingContentRect(Rect{100,200,1100,1000},dpi,Rect{});
            assert(content.left==100+pad&&content.top==200+pad&&content.right==1100-pad&&content.bottom==1000-pad);
            // Legacy custom values are not added on top; larger explicit values survive.
            content=ReaderReadingContentRect(Rect{0,0,1000,800},dpi,Rect{5,90,pad,110});
            assert(content.left==pad&&content.top==90&&content.right==1000-pad&&content.bottom==690);
            for(int w:{0,1,2,3,40,100}) for(int h:{0,1,2,3,40,100})
            {
                auto tiny=ReaderReadingContentRect(Rect{0,0,w,h},dpi,Rect{-9,1000000,1000000,-9});
                assert(tiny.left>=0&&tiny.right<=w&&tiny.top>=0&&tiny.bottom<=h);
                assert(tiny.right-tiny.left>=(w?1:0)&&tiny.bottom-tiny.top>=(h?1:0));
            }
        }
    }
    else if(test=="pdf_geometry")
        for(int dpi:{96,120,144,168,192})
        {
            struct Rect {int left,top,right,bottom;};
            auto content=ReaderReadingContentRect(Rect{0,0,ReaderScale(300,dpi),ReaderScale(400,dpi)},dpi,Rect{});
            int w=content.right-content.left,h=content.bottom-content.top;
            auto fit=ReaderPdfLayout(300,600,w,h,0,dpi,0,500000);
            assert(abs(fit.pageWidth-w)<=1&&fit.maxX==0&&fit.maxY>0&&fit.top<0);
            assert(content.left+fit.left>=ReaderReadingPadding(dpi));
            assert(content.left+fit.left+fit.pageWidth<=content.right);
            auto page=ReaderPdfLayout(300,600,w,h,-1,dpi,0,0);assert(page.maxX==0&&page.maxY==0);
            auto zoom=ReaderPdfLayout(300,600,w,h,200,dpi,500000,500000);assert(zoom.maxX>0&&zoom.maxY>0);
            assert(abs(ReaderPdfNormalize(-zoom.top,zoom.pageHeight)-500000)<10000);
            auto resized=ReaderPdfLayout(300,600,w+ReaderScale(40,dpi),h+ReaderScale(30,dpi),200,dpi,500000,500000);
            assert(abs(ReaderPdfNormalize(-resized.top,resized.pageHeight)-500000)<10000);
            assert(ReaderPdfZoom(400,1,dpi,1)==400&&ReaderPdfZoom(25,1,dpi,-1)==25);
        }
    else if(test=="pdf_history")
    {
        ReadingAnchor a(8,-1,5,false,100000,200000),b(8,-1,5,false,800000,0),next;
        assert(!(a==b));ReadingHistory history;history.Record(a,b);assert(history.Back(b,next)&&next==a);
        assert(history.Forward(a,next)&&next==b);
    }
    else if(test=="export")
    {
        bookmark_detail_t mark(-1,6);mark.title=L"中文 [标题]";mark.summary=L"原文*片段*";mark.note=L"我的备注\n第二行";
        auto md=ReaderExportBookmarks(L"Book #1",{mark},true),txt=ReaderExportBookmarks(L"Book #1",{mark},false);
        assert(md.find(L"# Book \\#1")!=std::wstring::npos&&md.find(L"\\[标题\\]")!=std::wstring::npos);
        assert(md.find(L"备注: 我的备注\n第二行")!=std::wstring::npos&&md.find(L"位置: 7")!=std::wstring::npos);
        assert(txt.find(L"中文 [标题]")!=std::wstring::npos&&txt.find(L"原文*片段*")!=std::wstring::npos);
        assert(ReaderExportBookmarks(L"empty",{},false)==L"empty\r\n\r\n");
    }
    else assert(false);
}
