#pragma once

#include "Cache.h"
#include <vector>

void OpenLibrary(HWND owner, Cache& cache);
void LibraryChanged();
void CloseLibrary();
BOOL LibraryTranslateMessage(MSG* message);

// UI-thread bridge: cache mutations always rebind the active reading anchor.
BOOL LibraryCanAcceptEntries();
BOOL IsLibraryBookFile(const TCHAR* path);
void ImportLibraryEntries(const std::vector<item_t>& entries);
void OpenLibraryBook(int id);
BOOL RemoveLibraryBook(int id);
int RemoveLibraryBooks(const std::vector<int>& ids);
void SetLibraryBooksState(const std::vector<int>& ids, int favorite, int state);
BOOL RelinkLibraryBook(int id, const TCHAR* path, const TCHAR* key);
void SaveLibrary();

const UINT WM_READER_LIBRARY_VIEW = WM_APP + 734;
void CloseLibraryView();
void LayoutLibraryView(HWND owner, const RECT* rect, UINT dpi, BOOL visible);
