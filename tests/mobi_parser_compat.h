#ifndef READER_TEST_MOBI_PARSER_COMPAT_H
#define READER_TEST_MOBI_PARSER_COMPAT_H

// Test-only substitutions for the Win32 base class and image/UI APIs. Production
// MobiBook.cpp and MobiBook.h are compiled unchanged; this is not a Windows build.
#define __BOOK_H__
#define __TYPES_H__
#define __UTILS_H__
#include <algorithm>
#include <cassert>
#include <climits>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <locale>
#include <map>
#include <string>
#include <vector>

using BOOL = int;
using UINT = unsigned int;
using BYTE = unsigned char;
using HWND = void *;
using TCHAR = wchar_t;
using WCHAR = wchar_t;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 4096
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define _countof(a) ARRAYSIZE(a)
#define _T(s) L##s

inline std::string TestUtf8(const std::wstring &value)
{
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(value);
}
inline std::wstring TestWide(const std::string &value)
{
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(value);
}
inline FILE *_wfopen(const wchar_t *filename, const wchar_t *mode)
{
    return std::fopen(TestUtf8(filename).c_str(), TestUtf8(mode).c_str());
}
struct IStream {
    std::vector<BYTE> bytes;
    void Release() { delete this; }
};
inline IStream *SHCreateMemStream(const BYTE *bytes, UINT size)
{
    if (!bytes || !size) return nullptr;
    IStream *stream = new IStream;
    stream->bytes.assign(bytes, bytes + size);
    return stream;
}
namespace Gdiplus {
    enum Status { Ok = 0, InvalidParameter = 2 };
    class Bitmap {
        Status status;
    public:
        explicit Bitmap(IStream *stream) : status(InvalidParameter)
        {
            // Validate resource plumbing only. Real GDI+ image decoding and
            // pixel rendering must be checked on Windows.
            if (stream && stream->bytes.size() >= 4) {
                const BYTE *data = stream->bytes.data();
                if ((data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') ||
                    (data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) ||
                    (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') ||
                    (data[0] == 'B' && data[1] == 'M')) status = Ok;
            }
        }
        Status GetLastStatus() const { return status; }
    };
}

enum book_type_t { book_unknown, book_text, book_epub, book_mobi, book_online, book_web };
struct chapter_item_t {
    int index = 0;
    std::wstring title;
    std::string url;
    int size = 0;
    int title_len = 0;
};
using chapters_t = std::vector<chapter_item_t>;
struct file_data_t { void *data; int size; };
using filelist_t = std::map<std::string, file_data_t>;
struct manifest_t { std::string id, href, media_type; };
using manifests_t = std::map<std::string, manifest_t *>;
using spines_t = std::vector<std::string>;
struct navpoint_t { std::string id, src, text; int order = 0; };
using navpoints_t = std::map<std::string, navpoint_t *>;

inline wchar_t *utf8_to_utf16(const char *source, int size, int *length)
{
    if (!source || size < 0 || !length) return nullptr;
    try {
        const std::wstring value = TestWide(std::string(source, size));
        wchar_t *result = static_cast<wchar_t *>(std::malloc((value.size() + 1) * sizeof(wchar_t)));
        if (!result) return nullptr;
        std::copy(value.begin(), value.end(), result);
        result[value.size()] = 0;
        *length = static_cast<int>(value.size());
        return result;
    } catch (...) { return nullptr; }
}

class Book {
protected:
    wchar_t m_fileName[MAX_PATH] = {};
    wchar_t *m_Text = nullptr;
    int m_Length = 0;
    BOOL m_bForceKill = FALSE;
    chapters_t m_Chapters;
    void ForceKill() {}
    BOOL GetLine(wchar_t *, int, int *, int *, int *, int *, int *);
    BOOL IsNewLine(wchar_t value) { return value == L'\r' || value == L'\n'; }
    virtual BOOL ParserBook(HWND) = 0;
    virtual Gdiplus::Bitmap *GetCover() { return nullptr; }
    virtual int GetTextBeginIndex() { return 0; }
    virtual BOOL DecodeText(const char *source, int size, wchar_t **out, int *length)
    {
        if (!source || size < 0 || !out || !length) return FALSE;
        try {
            std::wstring value = TestWide(std::string(source, size));
            *out = static_cast<wchar_t *>(std::malloc((value.size() + 1) * sizeof(wchar_t)));
            if (!*out) return FALSE;
            std::copy(value.begin(), value.end(), *out);
            (*out)[value.size()] = 0;
            *length = static_cast<int>(value.size());
            FormatText(*out, length);
            return TRUE;
        } catch (...) { return FALSE; }
    }
public:
    virtual ~Book() { std::free(m_Text); }
    virtual book_type_t GetBookType() = 0;
    virtual BOOL SaveBook(HWND) = 0;
    virtual BOOL UpdateChapters(int) = 0;
    virtual UINT GetOpenErrorMessage() { return 0; }
    BOOL FormatText(wchar_t *, int *);
    BOOL CloseBook()
    {
        std::free(m_Text);
        m_Text = nullptr;
        m_Length = 0;
        m_Chapters.clear();
        return TRUE;
    }
    void SetFileName(const wchar_t *filename)
    {
        std::wcsncpy(m_fileName, filename, MAX_PATH - 1);
        m_fileName[MAX_PATH - 1] = 0;
    }
};

inline int url_decode(const char *source, char *destination)
{
    char *begin = destination;
    while (*source) {
        if (*source == '%' && source[1] && source[2]) {
            char hex[3] = { source[1], source[2], 0 };
            char *end;
            const long value = std::strtol(hex, &end, 16);
            if (!*end) { *destination++ = static_cast<char>(value); source += 3; continue; }
        }
        *destination++ = *source++;
    }
    *destination = 0;
    return static_cast<int>(destination - begin);
}

#endif
