#pragma once
#include "types.h"
#include "BookmarkData.h"
BOOL EditReaderBookmarkNote(HWND owner, std::wstring& note);
BOOL ExportReaderBookmarks(HWND owner, const wchar_t* title, const std::vector<bookmark_detail_t>& marks);
