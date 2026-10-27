#pragma once
#include "types.h"
#include <commctrl.h>
#include "ThemePalette.h"

void ThemeConfigure(const header_t* settings);
void ThemeAttachWindow(HWND window);
void ThemeRefreshAll();
void ThemeReleaseMenus(HWND window);
void ThemeUpdateMenu(HWND window);
void ThemeShutdown();
const ReaderPalette& ThemeColors();
ReaderPalette ThemeControlPalette(HWND window);
ReaderColors ThemeReadingColors(const header_t* settings);
HBRUSH ThemeWindowBrush();
void ThemeSetCurrentChapter(HWND tree, HTREEITEM item);
void ApplyReaderTheme();
