#include "TitleBarTheme.h"
#include <string>

namespace {
// Documented DWMWINDOWATTRIBUTE values; also build with pre-Windows-11 SDKs.
const DWORD immersiveDarkMode = 20, captionColor = 35, textColor = 36;
const COLORREF systemColor = 0xffffffffu; // DWMWA_COLOR_DEFAULT
using SetAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

struct DwmApi
{
    HMODULE module = nullptr;
    SetAttribute set = nullptr;
    DwmApi()
    {
        // Resolve the public API from the system directory. Older systems can
        // start normally without a mandatory dwmapi.dll import or private APIs.
        wchar_t directory[MAX_PATH];
        UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) return;
        std::wstring path(directory, length); path += L"\\dwmapi.dll";
        module = LoadLibraryW(path.c_str());
        if (module) set = reinterpret_cast<SetAttribute>(GetProcAddress(module, "DwmSetWindowAttribute"));
        if (module && !set) { FreeLibrary(module); module = nullptr; }
    }
    ~DwmApi() { if (module) FreeLibrary(module); }
};
}

void ApplyNativeTitleBarTheme(HWND window, const ReaderPalette& palette, bool dark)
{
    if (!window || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return;
    static DwmApi api;
    if (!api.set) return;
    HIGHCONTRASTW high = {}; high.cbSize = sizeof(high);
    bool useSystem = !SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high), &high, 0)
        || (high.dwFlags & HCF_HIGHCONTRASTON);
    BOOL enabled = !useSystem && dark;
    HRESULT mode = api.set(window, immersiveDarkMode, &enabled, sizeof(enabled));
    bool custom = !useSystem && SUCCEEDED(mode);
    COLORREF background = custom ? palette.window & 0x00ffffffu : systemColor;
    COLORREF text = custom ? palette.text & 0x00ffffffu : systemColor;
    HRESULT backgroundResult = api.set(window, captionColor, &background, sizeof(background));
    HRESULT textResult = api.set(window, textColor, &text, sizeof(text));
    if (custom && (FAILED(backgroundResult) || FAILED(textResult)))
    {
        // Windows 10 may accept native dark mode but not explicit colors.
        // Reset the pair together so a partial override cannot impair contrast.
        api.set(window, captionColor, &systemColor, sizeof(systemColor));
        api.set(window, textColor, &systemColor, sizeof(systemColor));
    }
}
