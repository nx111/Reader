#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct ReaderPdfOutline { int page, depth; std::wstring title; };
struct ReaderPdfSize { double width, height; };
struct ReaderPdfRect { double left, bottom, right, top; };
struct ReaderPdfFrame
{
    int page = 0, width = 0, height = 0, pageWidth = 0, pageHeight = 0, left = 0, top = 0;
    double paperWidth = 0, paperHeight = 0;
    std::vector<uint8_t> pixels; // top-down, opaque BGRA, only the visible viewport
    std::wstring text;
    std::vector<ReaderPdfRect> characters; // parallel UTF-16 units, empty for scans
};
// All PDFium calls are serialized, including metadata/import and close. No V8,
// forms, external actions or JavaScript entry points are loaded by Reader.
class ReaderPdfDocument
{
    void* document = nullptr;
    FILE* file = nullptr;
    struct FileAccess;
    std::unique_ptr<FileAccess> access;
public:
    int error = 0, pages = 0;
    std::wstring title, author;
    std::vector<ReaderPdfOutline> outline;
    ReaderPdfDocument();
    ~ReaderPdfDocument();
    ReaderPdfDocument(const ReaderPdfDocument&) = delete;
    bool Open(const std::wstring& path, const std::wstring& libraryPath,
        const std::atomic<unsigned>* cancel = nullptr, unsigned expected = 0);
    void Close();
    ReaderPdfSize Size(int page);
    bool Render(ReaderPdfFrame& frame, const std::atomic<unsigned>& request, unsigned expected);
};
