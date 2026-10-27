#pragma once

#include "types.h"
#include "ReaderText.h"

struct reader_search_source_t
{
    ReaderSearchToken token;
    int base;
    std::wstring text, scope;
    std::vector<ReaderTextChapter> chapters;
    reader_search_source_t() : base(0) {}
};

void OpenReaderSearch(HWND owner);
void InvalidateReaderSearch();
void CloseReaderSearch();
BOOL ReaderSearchTranslate(MSG* message);
void AdvanceReaderSearch(BOOL previous);

BOOL CaptureReaderSearch(reader_search_source_t* source);
BOOL ActivateReaderSearch(const ReaderSearchToken& token, int position, int length);
std::wstring ReaderSearchScope();
void ClearReaderSearchHighlight();
