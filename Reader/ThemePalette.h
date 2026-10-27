#pragma once
#include <cstdint>

inline uint32_t ThemeRgb(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }
struct ReaderPalette
{
    uint32_t window, surface, raised, text, muted, border, hover, pressed;
    uint32_t accent, selected, selectedText, disabled, hit, hitText;
};
inline ReaderPalette MakeReaderPalette(int dark)
{
    if (dark) return { ThemeRgb(24,26,30), ThemeRgb(32,36,43), ThemeRgb(40,46,55),
        ThemeRgb(231,235,240), ThemeRgb(173,181,194), ThemeRgb(104,118,137),
        ThemeRgb(50,58,70), ThemeRgb(62,74,91), ThemeRgb(109,181,255),
        ThemeRgb(39,91,145), ThemeRgb(255,255,255), ThemeRgb(160,170,184),
        ThemeRgb(238,192,73), ThemeRgb(24,26,30) };
    return { ThemeRgb(243,245,248), ThemeRgb(255,255,255), ThemeRgb(231,236,243),
        ThemeRgb(24,33,44), ThemeRgb(72,87,106), ThemeRgb(104,120,141),
        ThemeRgb(221,232,247), ThemeRgb(201,218,240), ThemeRgb(21,93,179),
        ThemeRgb(27,90,154), ThemeRgb(255,255,255), ThemeRgb(93,107,125),
        ThemeRgb(255,230,110), ThemeRgb(0,0,0) };
}
struct ReaderColors { uint32_t background, text, title; bool image; };
inline ReaderColors ResolveReaderColors(int theme, bool custom, uint32_t background,
    uint32_t text, uint32_t title, bool image)
{
    ReaderPalette p = MakeReaderPalette(theme);
    return custom ? ReaderColors{background, text, title, image} : ReaderColors{p.surface, p.text, p.text, false};
}
