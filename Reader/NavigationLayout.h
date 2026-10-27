#pragma once
#include "ReadingState.h"
#include "CommandIds.h"
#include <cwctype>
#include <string>

struct ReaderNavigationGeometry
{
    int width, headerHeight, footerTop, footerHeight, viewportHeight, itemHeight, contentHeight, scrollMaximum;
    bool compact;
};
inline ReaderNavigationGeometry ReaderNavigationLayout(int width,int height,int dpi,bool visible,bool panel,bool focus=false)
{
    width=(std::max)(0,width);height=(std::max)(0,height);dpi=(std::max)(96,dpi);
    ReaderNavigationGeometry g={};g.width=ReaderNavigationWidth(width,dpi,visible,panel,focus);
    g.compact=g.width<ReaderScale(160,dpi);g.headerHeight=focus?0:(std::min)(height,ReaderScale(44,dpi));
    g.footerHeight=(std::min)((std::max)(0,height-g.headerHeight),ReaderScale(g.compact?76:48,dpi));
    g.footerTop=height-g.footerHeight;g.viewportHeight=(std::max)(0,g.footerTop-g.headerHeight);
    g.itemHeight=ReaderScale(40,dpi);g.contentHeight=g.itemHeight*7;
    g.scrollMaximum=(std::max)(0,g.contentHeight-g.viewportHeight);return g;
}
inline int ReaderNavigationCommand(int index)
{ return index==0?IDM_NAV_BACK:index==1?IDM_NAV_FORWARD:0; }
struct ReaderLibraryGeometry
{
    int sidebarWidth,left,width,searchTop,searchWidth,filterLeft,filterWidth;
    int actionsTop,actionSize,actionGap,actionColumns,listTop,listHeight,statusTop,gap;
    bool sidebar;
};
inline ReaderLibraryGeometry ReaderLibraryLayout(int width,int height,int dpi,bool allowSidebar=true)
{
    width=(std::max)(0,width);height=(std::max)(0,height);dpi=(std::max)(96,dpi);
    ReaderLibraryGeometry g={};g.gap=ReaderScale(16,dpi);
    g.sidebar=allowSidebar && width>=ReaderScale(680,dpi);g.sidebarWidth=g.sidebar?ReaderScale(176,dpi):0;
    g.left=(std::min)(width,g.sidebarWidth+g.gap);g.width=(std::max)(0,width-g.left-g.gap);
    g.searchTop=ReaderScale(16,dpi);g.filterWidth=g.sidebar?0:(std::min)(ReaderScale(104,dpi),g.width/3);
    g.searchWidth=(std::max)(0,g.width-g.filterWidth-(g.filterWidth?ReaderScale(8,dpi):0));g.filterLeft=g.left+g.width-g.filterWidth;
    g.actionsTop=ReaderScale(60,dpi);g.actionSize=ReaderScale(28,dpi);g.actionGap=ReaderScale(4,dpi);
    g.actionColumns=(std::max)(1,(std::min)(9,(g.width+g.actionGap)/(g.actionSize+g.actionGap)));
    g.listTop=g.actionsTop+((9+g.actionColumns-1)/g.actionColumns)*(g.actionSize+g.actionGap)+ReaderScale(12,dpi);
    g.statusTop=(std::max)(g.listTop,height-ReaderScale(32,dpi));g.listHeight=(std::max)(0,g.statusTop-g.listTop-ReaderScale(8,dpi));return g;
}
enum ReaderNavigationKeyAction { navigation_key_none,navigation_key_toggle,navigation_key_leave,navigation_key_previous,navigation_key_next,navigation_key_first,navigation_key_last,navigation_key_activate };
inline ReaderNavigationKeyAction ReaderNavigationKey(unsigned key,bool alt,bool control,bool shift,bool focused)
{
    if(key==0x79 && !control && !shift) return navigation_key_toggle;
    if(!focused || alt || control) return navigation_key_none;
    if(key==0x1b) return navigation_key_leave;
    if(key==0x25 || key==0x26) return navigation_key_previous;
    if(key==0x27 || key==0x28) return navigation_key_next;
    if(key==0x24) return navigation_key_first;if(key==0x23) return navigation_key_last;
    if(key==0x0d || key==0x20) return navigation_key_activate;
    return navigation_key_none;
}
class ReaderAltNavigationGesture
{
    bool armed=false;
public:
    void Down(unsigned key,bool control,bool repeat) { if(key==0x12) {if(!repeat) armed=!control;} else armed=false; }
    bool Up(unsigned key) { if(key!=0x12) return false;bool value=armed;armed=false;return value; }
    void Cancel() {armed=false;}
};
inline int ReaderNavigationCycle(int count,int index,int direction)
{return count>0?((index+direction)%count+count)%count:-1;}
inline wchar_t ReaderMenuMnemonic(const std::wstring& label)
{
    for(size_t i=0;i+1<label.size();++i) if(label[i]==L'&') {if(label[i+1]==L'&') ++i;else return (wchar_t)towupper(label[i+1]);}
    return 0;
}
