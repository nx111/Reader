#pragma once
#include "framework.h"
#include "ThemePalette.h"

// Native non-client colors only; client painting and window opacity stay separate.
void ApplyNativeTitleBarTheme(HWND window, const ReaderPalette& palette, bool dark);
