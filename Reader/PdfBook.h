#pragma once
#include "Book.h"
#include "PdfEngine.h"
#include "PdfLayout.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

const UINT WM_READER_PDF_FRAME = WM_APP + 735;
std::wstring ReaderPdfLibraryPath();
class PdfBook : public Book
{
    struct Request
    {
        int page=0,width=0,height=0,zoom=0,x=0,y=0,dpi=96;
        bool operator==(const Request& a) const { return page==a.page&&width==a.width&&height==a.height&&zoom==a.zoom&&x==a.x&&y==a.y&&dpi==a.dpi; }
    } wanted;
    struct Cached { Request request; std::shared_ptr<ReaderPdfFrame> frame; ReaderPdfViewport geometry; };
    std::deque<Cached> cache;
    std::thread worker;
    std::mutex lock;
    std::condition_variable wake;
    std::atomic<unsigned> revision{1};
    std::atomic<bool> loading{true},stopping{false};
    unsigned handled=1;
    bool haveRequest=false,failed=false;
    int zoom=0,x=0,y=0,highlight=-1,highlightLength=0;
    HWND owner=nullptr;
    UINT openError=0;
    ReaderPdfViewport geometry={};
    ReaderPdfDocument document;
    GdiSurface alpha;
    void Work(std::wstring path, UINT token);
    void RequestFrame(int width,int height,int dpi);
public:
    PdfBook() {}
    ~PdfBook();
    book_type_t GetBookType() override { return book_pdf; }
    BOOL OpenBook(HWND window) override;
    BOOL IsLoading() override { return loading; }
    UINT GetOpenErrorMessage() override { return openError; }
    BOOL SaveBook(HWND) override { return FALSE; }
    BOOL UpdateChapters(int) override { return TRUE; }
    void DrawPage(HWND window,HDC dc,RECT* rect,BOOL enableAlpha) override;
    HDC AlphaDC() const override { return alpha.DC(); }
    BOOL IsBlankPage() override { return FALSE; }
    void RestoreView(const item_t* item);
    void SaveView(item_t* item);
    void SetReadingPosition(int page) override;
    void JumpChapter(HWND window,int chapter) override;
    void JumpPrevChapter(HWND window) override;
    void JumpNextChapter(HWND window) override;
    int GetCurChapterIndex() override;
    void Zoom(HWND window,int direction);
    void Fit(HWND window,bool page);
    void Scroll(HWND window,int pixels,bool horizontal=false,bool absolute=false);
    bool ScrollBar(HWND window,int bar,WPARAM command);
    BOOL SearchSnapshot(std::wstring& text);
    BOOL FindHit(int position,int length);
    void SetSearchHighlight(int position,int length) override { highlight=position;highlightLength=length; }
    bool FrameReady();
    void UpdateScrollbars(HWND viewport);
    std::wstring Title() const { return document.title; }
    std::wstring Author() const { return document.author; }
protected:
    BOOL ParserBook(HWND) override { return FALSE; }
    BOOL IsValid() override { return !loading && document.pages>0 && m_pIndex; }
    BOOL OnUpDownEvent(HWND window,int type) override;
};
