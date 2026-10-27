#include "PdfBook.h"
#include "DPIAwareness.h"
#include "Theme.h"
#include "resource.h"
#include <algorithm>

std::wstring ReaderPdfLibraryPath()
{
    wchar_t path[MAX_PATH]={}; DWORD length=GetModuleFileName(nullptr,path,MAX_PATH);
    if(!length || length>=MAX_PATH) return L"";
    wchar_t* name=wcsrchr(path,L'\\'); if (!name) return L"";
    *(name+1)=0; return std::wstring(path)+L"pdfium.dll";
}
PdfBook::~PdfBook()
{
    stopping=true; ++revision; wake.notify_one();
    if (worker.joinable()) worker.join(); // cooperative cancellation; never terminate a PDFium thread
}
BOOL PdfBook::OpenBook(HWND window)
{
    owner=window;m_OpenToken=NextOpenToken();UINT token=m_OpenToken;
    try { worker=std::thread(&PdfBook::Work,this,std::wstring(m_fileName),token); }
    catch (...) { loading=false;openError=IDS_PDF_COMPONENT;PostMessage(window,WM_OPEN_BOOK,FALSE,token);return FALSE; }
    return TRUE;
}
void PdfBook::Work(std::wstring path,UINT token)
{
    bool ok=false;
    try { ok=document.Open(path,ReaderPdfLibraryPath(),&revision,1); } catch (...) { document.error=3; }
    if (ok)
    {
        m_Length=document.pages;m_PageLength=1;
        for (const auto& entry:document.outline)
        { chapter_item_t chapter={};chapter.index=entry.page;chapter.title=std::wstring(entry.depth*2,L' ')+entry.title;m_Chapters.push_back(chapter); }
    }
    else openError=document.error==1000?IDS_PDF_COMPONENT:document.error==1001?IDS_PDF_TOO_LARGE:document.error==4?IDS_PDF_PASSWORD:IDS_PDF_INVALID;
    loading=false;
    if (!stopping) PostMessage(owner,WM_OPEN_BOOK,ok,token);
    if (!ok) return;
    while (!stopping)
    {
        Request request;unsigned current;
        { std::unique_lock<std::mutex> guard(lock);wake.wait(guard,[this] {return stopping || (haveRequest&&revision!=handled);});if(stopping) break;request=wanted;current=revision;handled=current; }
        auto size=document.Size(request.page);
        auto layout=ReaderPdfLayout(size.width,size.height,request.width,request.height,request.zoom,request.dpi,request.x,request.y);
        auto frame=std::make_shared<ReaderPdfFrame>();frame->page=request.page;frame->width=request.width;frame->height=request.height;
        frame->pageWidth=layout.pageWidth;frame->pageHeight=layout.pageHeight;frame->left=layout.left;frame->top=layout.top;
        frame->paperWidth=size.width;frame->paperHeight=size.height;
        bool rendered=false;
        try { rendered=document.Render(*frame,revision,current); } catch (...) { rendered=false; }
        { std::lock_guard<std::mutex> guard(lock);if(stopping||revision!=current) continue;
          failed=!rendered;
          if(rendered) {cache.push_front({request,frame,layout});size_t bytes=0;
            for(auto it=cache.begin();it!=cache.end();) {bytes+=it->frame->pixels.size()+it->frame->characters.size()*sizeof(ReaderPdfRect)+it->frame->text.size()*sizeof(wchar_t);if(it!=cache.begin()&&(bytes>64*1024*1024 || it-cache.begin()>=2)) it=cache.erase(it);else ++it;} }
        }
        if(!stopping) PostMessage(owner,WM_READER_PDF_FRAME,current,token);
    }
}
void PdfBook::RestoreView(const item_t* item)
{ if(item) {zoom=item->pdf_zoom;x=item->pdf_x;y=item->pdf_scroll;} if(m_pIndex&&m_Length>0) m_Index=(std::max)(0,(std::min)(m_Length-1,m_Index)); }
void PdfBook::SaveView(item_t* item)
{ if(item) {item->pdf_zoom=zoom;item->pdf_x=x;item->pdf_scroll=y;} }
void PdfBook::SetReadingPosition(int page)
{
    Page::SetReadingPosition(page);x=y=0;highlight=-1;++revision;
}
void PdfBook::JumpChapter(HWND window,int chapter)
{
    if (chapter<0 || chapter>=(int)m_Chapters.size()) return;
    SetReadingPosition(m_Chapters[chapter].index);ReDraw(window);
}
void PdfBook::JumpPrevChapter(HWND window)
{
    if(!IsValid()) return;int page=-1;
    for(const auto& chapter:m_Chapters) if(chapter.index<m_Index && chapter.index>page) page=chapter.index;
    if(page>=0) {SetReadingPosition(page);ReDraw(window);}
}
void PdfBook::JumpNextChapter(HWND window)
{
    if(!IsValid()) return;int page=m_Length;
    for(const auto& chapter:m_Chapters) if(chapter.index>m_Index && chapter.index<page) page=chapter.index;
    if(page<m_Length) {SetReadingPosition(page);ReDraw(window);}
}
int PdfBook::GetCurChapterIndex()
{
    if(!m_pIndex) return -1;int found=-1,closest=-1;
    for(size_t i=0;i<m_Chapters.size();++i) if(m_Chapters[i].index<=m_Index && m_Chapters[i].index>=closest) {found=(int)i;closest=m_Chapters[i].index;}
    return found;
}
void PdfBook::RequestFrame(int width,int height,int dpi)
{
    if(!IsValid()) return;Request request;request.page=m_Index;request.width=width;request.height=height;request.zoom=zoom;request.x=x;request.y=y;request.dpi=dpi;
    std::lock_guard<std::mutex> guard(lock);
    if(haveRequest&&wanted==request) return;
    wanted=request;haveRequest=true;failed=false;++revision;
    for(auto& found:cache) if(found.request==request) {geometry=found.geometry;handled=revision;return;}
    wake.notify_one();
}
void PdfBook::DrawPage(HWND window,HDC dc,RECT* rect,BOOL enableAlpha)
{
    if(!IsValid()) return;
    int width=rect->right-rect->left,height=rect->bottom-rect->top;
    RequestFrame(width,height,GetWindowDpiCompat(window));
    std::shared_ptr<ReaderPdfFrame> frame;bool error=false;
    {std::lock_guard<std::mutex> guard(lock);for(auto& entry:cache) if(entry.request==wanted) {frame=entry.frame;geometry=entry.geometry;break;}error=failed;}
    HDC target=dc;
    if(enableAlpha) {if(!alpha.Ensure(dc,width,height)) return;target=alpha.DC();}
    if(frame)
    {
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetDIBitsToDevice(target,rect->left,rect->top,width,height,0,0,0,height,frame->pixels.data(),&info,DIB_RGB_COLORS);
        if(highlight>=0 && highlightLength>0 && highlight<(int)frame->characters.size())
        {
            int saved=SaveDC(target);IntersectClipRect(target,rect->left,rect->top,rect->right,rect->bottom);
            HPEN pen=CreatePen(PS_SOLID,2,ThemeColors().hitText);SelectObject(target,pen);SelectObject(target,GetStockObject(HOLLOW_BRUSH));
            for(int i=highlight;i<(int)frame->characters.size()&&i<highlight+highlightLength;++i)
            {
                const auto& box=frame->characters[i];
                double sx=frame->pageWidth/frame->paperWidth,sy=frame->pageHeight/frame->paperHeight;
                Rectangle(target,rect->left+frame->left+(int)(box.left*sx),rect->top+frame->top+frame->pageHeight-(int)(box.top*sy),
                    rect->left+frame->left+(int)(box.right*sx),rect->top+frame->top+frame->pageHeight-(int)(box.bottom*sy));
            }
            RestoreDC(target,saved);DeleteObject(pen);
        }
    }
    else
    {
        HBRUSH brush=CreateSolidBrush(ThemeColors().surface);FillRect(target,rect,brush);DeleteObject(brush);SetTextColor(target,ThemeColors().text);SetBkMode(target,TRANSPARENT);
        RECT text=*rect;InflateRect(&text,-12,-12);
        DrawText(target,error?L"PDF 页面无法呈现，尝试翻页或重新打开。":L"正在呈现 PDF 页面…",-1,&text,DT_CENTER|DT_WORDBREAK);
    }
}
bool PdfBook::FrameReady()
{std::lock_guard<std::mutex> guard(lock);for(auto& entry:cache) if(entry.request==wanted) return true;return false;}
BOOL PdfBook::OnUpDownEvent(HWND window,int type)
{
    if(!IsValid()) return FALSE;
    bool down=type==DRAW_PAGE_DOWN||type==DRAW_LINE_DOWN;
    if(type==DRAW_LINE_DOWN||type==DRAW_LINE_UP)
    {
        int position=(int)(std::min)((int64_t)geometry.maxY,(int64_t)geometry.pageHeight*y/1000000);
        if((down&&position<geometry.maxY)||(!down&&position>0)) {Scroll(window,MulDiv(down?48:-48,GetWindowDpiCompat(window),96));return FALSE;}
    }
    int next=(std::max)(0,(std::min)(m_Length-1,m_Index+(down?1:-1)));
    if(next!=m_Index) {SetReadingPosition(next);ReDraw(window);} return FALSE;
}
void PdfBook::Zoom(HWND window,int direction)
{zoom=ReaderPdfZoom(zoom,geometry.scale,GetWindowDpiCompat(window),direction);ReDraw(window);}
void PdfBook::Fit(HWND window,bool page) {zoom=page?-1:0;x=y=0;ReDraw(window);}
void PdfBook::Scroll(HWND window,int pixels,bool horizontal,bool absolute)
{
    int maximum=horizontal?geometry.maxX:geometry.maxY,extent=horizontal?geometry.pageWidth:geometry.pageHeight;int& position=horizontal?x:y;
    int previous=(int)(std::min)((int64_t)maximum,(int64_t)extent*position/1000000);
    position=ReaderPdfNormalize((std::max)(0,(std::min)(maximum,absolute?pixels:previous+pixels)),extent);ReDraw(window);
}
bool PdfBook::ScrollBar(HWND window,int bar,WPARAM command)
{
    SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,bar,&info);
    int value=info.nPos;
    switch(LOWORD(command)) {case SB_LINEUP:value-=48;break;case SB_LINEDOWN:value+=48;break;
    case SB_PAGEUP:value-=(int)info.nPage;break;case SB_PAGEDOWN:value+=(int)info.nPage;break;
    case SB_THUMBTRACK:case SB_THUMBPOSITION:value=info.nTrackPos;break;case SB_TOP:value=0;break;case SB_BOTTOM:value=info.nMax;break;default:return false;}
    Scroll(GetParent(window),value,bar==SB_HORZ,true);return true;
}
BOOL PdfBook::SearchSnapshot(std::wstring& text)
{std::lock_guard<std::mutex> guard(lock);for(auto& entry:cache) if(entry.request==wanted) {text=entry.frame->text;return TRUE;}return FALSE;}
BOOL PdfBook::FindHit(int position,int length)
{
    std::lock_guard<std::mutex> guard(lock);
    for(auto& entry:cache) if(entry.request==wanted && position>=0 && length>0 && position<=(int)entry.frame->text.size()-length)
    {
        highlight=position;highlightLength=length;const auto& frame=*entry.frame;
        if(position<(int)frame.characters.size())
        {
            const auto& box=frame.characters[position];
            y=ReaderPdfNormalize((std::min)(entry.geometry.maxY,(int)(frame.pageHeight-box.top*frame.pageHeight/frame.paperHeight)-32),frame.pageHeight);
            x=ReaderPdfNormalize((std::min)(entry.geometry.maxX,(int)(box.left*frame.pageWidth/frame.paperWidth)-32),frame.pageWidth);
        }
        return TRUE;
    }return FALSE;
}

void PdfBook::UpdateScrollbars(HWND viewport)
{
    for (int bar : {SB_VERT,SB_HORZ})
    {
        SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
        info.nMax=(bar==SB_VERT?geometry.pageHeight:geometry.pageWidth)-1;
        info.nPage=bar==SB_VERT?geometry.height:geometry.width;
        info.nPos=(int)(std::min)((int64_t)(bar==SB_VERT?geometry.maxY:geometry.maxX),
            (int64_t)(bar==SB_VERT?geometry.pageHeight:geometry.pageWidth)*(bar==SB_VERT?y:x)/1000000);
        SetScrollInfo(viewport,bar,&info,TRUE);
        ShowScrollBar(viewport,bar,(bar==SB_VERT?geometry.maxY:geometry.maxX)>0);
    }
}
