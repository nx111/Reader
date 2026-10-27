#include "PdfEngine.h"
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <fpdf_progressive.h>
#include <algorithm>
#include <climits>
#include <mutex>
#include <set>
#include <codecvt>
#include <locale>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
std::mutex engineLock;
struct Api
{
    void* module = nullptr;
#define PDF_FUNCTIONS(X) \
    X(FPDF_InitLibraryWithConfig) X(FPDF_SetSandBoxPolicy) X(FPDF_LoadCustomDocument) X(FPDF_CloseDocument) \
    X(FPDF_GetLastError) X(FPDF_GetPageCount) X(FPDF_GetPageSizeByIndexF) X(FPDF_LoadPage) X(FPDF_ClosePage) \
    X(FPDF_GetMetaText) X(FPDFBitmap_CreateEx) X(FPDFBitmap_Destroy) X(FPDFBitmap_FillRect) \
    X(FPDF_RenderPageBitmap_Start) X(FPDF_RenderPage_Continue) X(FPDF_RenderPage_Close) \
    X(FPDFText_LoadPage) X(FPDFText_ClosePage) X(FPDFText_CountChars) X(FPDFText_GetUnicode) X(FPDFText_GetCharBox) \
    X(FPDFBookmark_GetFirstChild) X(FPDFBookmark_GetNextSibling) X(FPDFBookmark_GetTitle) X(FPDFBookmark_GetDest) X(FPDFDest_GetDestPageIndex)
#define MEMBER(name) decltype(&::name) name = nullptr;
    PDF_FUNCTIONS(MEMBER)
#undef MEMBER
    bool Load(const std::wstring& path)
    {
        if (module) return true;
#ifdef _WIN32
        HMODULE loaded = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#define SYMBOL(name) GetProcAddress(loaded, #name)
#else
        auto narrow = std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(path);
        void* loaded = dlopen(narrow.c_str(), RTLD_NOW | RTLD_LOCAL);
#define SYMBOL(name) dlsym(loaded, #name)
#endif
        if (!loaded) return false;
        bool ok = true;
#define BIND(name) name = reinterpret_cast<decltype(name)>(SYMBOL(name)); ok = ok && name;
        PDF_FUNCTIONS(BIND)
#undef BIND
#undef SYMBOL
        if (!ok)
        {
#ifdef _WIN32
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            return false;
        }
        FPDF_LIBRARY_CONFIG config = {}; config.version = 2;
        FPDF_InitLibraryWithConfig(&config); FPDF_SetSandBoxPolicy(FPDF_POLICY_MACHINETIME_ACCESS, 0);
        module = loaded; return true; // retained for process lifetime; no unload under concurrent imports
    }
} api;
std::wstring Wide(const std::vector<unsigned short>& buffer)
{
    std::wstring text;
    for (size_t i = 0; i < buffer.size() && buffer[i]; ++i) text += (wchar_t)buffer[i];
    return text;
}
std::wstring Meta(FPDF_DOCUMENT doc, const char* key)
{
    unsigned long size = api.FPDF_GetMetaText(doc, key, nullptr, 0);
    if (size < 2 || size > 65536) return L"";
    std::vector<unsigned short> value(size / 2 + 1);
    api.FPDF_GetMetaText(doc, key, value.data(), size); return Wide(value);
}
struct Pause : IFSDK_PAUSE
{
    Pause(const std::atomic<unsigned>&, unsigned)
    {
        version = 1; user = nullptr;
        NeedToPauseNow = [](IFSDK_PAUSE*) -> FPDF_BOOL { return 1; };
    }
};
}
struct ReaderPdfDocument::FileAccess : FPDF_FILEACCESS
{
    FILE* source;
    const std::atomic<unsigned>* cancel;
    unsigned expected;
    explicit FileAccess(FILE* file, unsigned long size, const std::atomic<unsigned>* token, unsigned current)
        : source(file), cancel(token), expected(current)
    {
        m_FileLen = size; m_Param = this;
        m_GetBlock = [](void* context, unsigned long position, unsigned char* buffer, unsigned long size) -> int
        {
            auto* access=(FileAccess*)context;
            if (access->cancel && *access->cancel != access->expected) return 0;
            if (position>access->m_FileLen || size>access->m_FileLen-position) return 0;
#ifdef _WIN32
            if (_fseeki64(access->source, position, SEEK_SET)) return 0;
#else
            if (fseeko(access->source, position, SEEK_SET)) return 0;
#endif
            return fread(buffer, 1, size, access->source) == size;
        };
    }
};
ReaderPdfDocument::ReaderPdfDocument() {}
ReaderPdfDocument::~ReaderPdfDocument() { Close(); }
void ReaderPdfDocument::Close()
{
    std::lock_guard<std::mutex> lock(engineLock);
    if (document) api.FPDF_CloseDocument((FPDF_DOCUMENT)document);
    document = nullptr; if (file) fclose(file); file = nullptr; access.reset(); pages = 0;
}
bool ReaderPdfDocument::Open(const std::wstring& path, const std::wstring& libraryPath,
    const std::atomic<unsigned>* cancel, unsigned expected)
{
    Close(); error = 0; title.clear(); author.clear(); outline.clear();
    std::lock_guard<std::mutex> lock(engineLock);
    if (!api.Load(libraryPath)) { error = 1000; return false; }
#ifdef _WIN32
    file = _wfopen(path.c_str(), L"rb");
    if (file) _fseeki64(file, 0, SEEK_END);
    long long size = file ? _ftelli64(file) : 0;
#else
    file = fopen(std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(path).c_str(), "rb");
    if (file) fseeko(file, 0, SEEK_END);
    long long size = file ? ftello(file) : 0;
#endif
    // PDFium's custom stream uses a 32-bit file length on Windows, including x64.
    if (size <= 0 || (unsigned long long)size > UINT32_MAX) { error = size > 0 ? 1001 : FPDF_ERR_FILE; return false; }
    access.reset(new FileAccess(file, (unsigned long)size, cancel, expected));
    document = api.FPDF_LoadCustomDocument(access.get(), nullptr);
    if (!document) { error = (int)api.FPDF_GetLastError(); return false; }
    access->cancel = nullptr;
    pages = api.FPDF_GetPageCount((FPDF_DOCUMENT)document);
    if (pages <= 0) { error = FPDF_ERR_FORMAT; return false; }
    title = Meta((FPDF_DOCUMENT)document, "Title"); author = Meta((FPDF_DOCUMENT)document, "Author");
    std::set<FPDF_BOOKMARK> seen;
    std::vector<std::pair<FPDF_BOOKMARK,int>> pending;
    pending.push_back({api.FPDFBookmark_GetFirstChild((FPDF_DOCUMENT)document, nullptr), 0});
    while (!pending.empty() && seen.size() < 4096 && (!cancel || *cancel==expected))
    {
        auto current = pending.back(); pending.pop_back();
        if (!current.first || !seen.insert(current.first).second) continue;
        pending.push_back({api.FPDFBookmark_GetNextSibling((FPDF_DOCUMENT)document, current.first), current.second});
        if (current.second < 32) pending.push_back({api.FPDFBookmark_GetFirstChild((FPDF_DOCUMENT)document, current.first), current.second+1});
        auto dest = api.FPDFBookmark_GetDest((FPDF_DOCUMENT)document, current.first);
        int page = dest ? api.FPDFDest_GetDestPageIndex((FPDF_DOCUMENT)document, dest) : -1;
        unsigned long size = api.FPDFBookmark_GetTitle(current.first, nullptr, 0);
        if (page < 0 || page >= pages || size < 2 || size > 65536) continue;
        std::vector<unsigned short> text(size / 2 + 1); api.FPDFBookmark_GetTitle(current.first, text.data(), size);
        outline.push_back({page,current.second,Wide(text)});
    }
    return true;
}
ReaderPdfSize ReaderPdfDocument::Size(int page)
{
    std::lock_guard<std::mutex> lock(engineLock); FS_SIZEF size = {};
    if (document && page >= 0 && page < pages) api.FPDF_GetPageSizeByIndexF((FPDF_DOCUMENT)document, page, &size);
    return {size.width,size.height};
}
bool ReaderPdfDocument::Render(ReaderPdfFrame& frame, const std::atomic<unsigned>& request, unsigned expected)
{
    if (frame.width <= 0 || frame.height <= 0 || (uint64_t)frame.width * frame.height > 8 * 1024 * 1024
        || frame.pageWidth <= 0 || frame.pageHeight <= 0 || request != expected) return false;
    frame.text.clear();frame.characters.clear();
    std::lock_guard<std::mutex> lock(engineLock);
    if (!document || frame.page < 0 || frame.page >= pages || request != expected) return false;
    FPDF_PAGE page = api.FPDF_LoadPage((FPDF_DOCUMENT)document, frame.page); if (!page) return false;
    frame.pixels.assign((size_t)frame.width * frame.height * 4, 255);
    FPDF_BITMAP bitmap = api.FPDFBitmap_CreateEx(frame.width,frame.height,FPDFBitmap_BGRA,frame.pixels.data(),frame.width*4);
    bool ok = false;
    if (bitmap)
    {
        Pause pause(request,expected);
        int status = api.FPDF_RenderPageBitmap_Start(bitmap,page,frame.left,frame.top,frame.pageWidth,frame.pageHeight,0,
            FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE,&pause);
        while (status == FPDF_RENDER_TOBECONTINUED && request == expected)
            status = api.FPDF_RenderPage_Continue(page,&pause);
        ok = status == FPDF_RENDER_DONE && request == expected;
        api.FPDF_RenderPage_Close(page); api.FPDFBitmap_Destroy(bitmap);
    }
    if (ok)
    {
        auto text = api.FPDFText_LoadPage(page);
        int count = text ? api.FPDFText_CountChars(text) : 0;
        for (int i=0;i<count && i<250000 && request==expected;++i)
        {
            unsigned unicode = api.FPDFText_GetUnicode(text,i); ReaderPdfRect rect = {};
            api.FPDFText_GetCharBox(text,i,&rect.left,&rect.right,&rect.bottom,&rect.top);
            if (!unicode || unicode > 0x10ffff) continue;
            if (unicode > 0xffff) { frame.text += (wchar_t)(0xd800+((unicode-0x10000)>>10)); frame.characters.push_back(rect); unicode=0xdc00+((unicode-0x10000)&1023); }
            frame.text += (wchar_t)unicode; frame.characters.push_back(rect);
        }
        if (text) api.FPDFText_ClosePage(text);
    }
    api.FPDF_ClosePage(page);
    return ok && request == expected;
}
