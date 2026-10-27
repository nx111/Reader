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
    if (dark) return { ThemeRgb(30,31,34), ThemeRgb(37,38,42), ThemeRgb(45,46,51),
        ThemeRgb(232,233,237), ThemeRgb(170,173,181), ThemeRgb(60,62,68),
        ThemeRgb(52,54,60), ThemeRgb(66,69,76), ThemeRgb(192,195,203),
        ThemeRgb(68,71,79), ThemeRgb(255,255,255), ThemeRgb(140,143,151),
        ThemeRgb(238,205,112), ThemeRgb(24,26,30) };
    return { ThemeRgb(248,248,247), ThemeRgb(255,255,255), ThemeRgb(242,243,244),
        ThemeRgb(31,32,36), ThemeRgb(100,103,110), ThemeRgb(224,225,228),
        ThemeRgb(235,236,239), ThemeRgb(220,222,227), ThemeRgb(60,63,70),
        ThemeRgb(49,51,56), ThemeRgb(255,255,255), ThemeRgb(121,124,132),
        ThemeRgb(255,234,155), ThemeRgb(0,0,0) };
}
struct ReaderControlColors { uint32_t background, ink; };
inline ReaderControlColors ReaderFlatControlColors(const ReaderPalette& p, bool enabled, bool hot, bool pressed, bool selected = false)
{
    return { selected ? p.selected : pressed ? p.pressed : hot ? p.hover : p.window,
        !enabled ? p.disabled : selected ? p.selectedText : p.text };
}
struct ReaderColors { uint32_t background, text, title; bool image; };
inline ReaderColors ResolveReaderColors(int theme, bool custom, uint32_t background,
    uint32_t text, uint32_t title, bool image)
{
    ReaderPalette p = MakeReaderPalette(theme);
    return custom ? ReaderColors{background, text, title, image} : ReaderColors{p.surface, p.text, p.text, false};
}
