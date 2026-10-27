#pragma once
#include "framework.h"

// Native nonclient metrics/ranges remain in place. App-owned sibling strips
// cover only those metrics; hover never changes the target's client rectangle.
void ReaderScrollbarsAttach(HWND window);
void ReaderScrollbarsUpdate(HWND window);
void ReaderScrollbarsRefresh(HWND root,BOOL cancel=FALSE);
void ReaderScrollbarsShutdown();
