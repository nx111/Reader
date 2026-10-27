#pragma once

// Standard WS_EX_LAYERED / WS_EX_TRANSPARENT bits. Keep this policy portable
// so cache normalization and every native window-style entry share one rule.
template<class Style> inline Style ReaderOpaqueExStyle(Style style)
{return style & ~(Style(0x00080000u)|Style(0x00000020u));}
template<class Settings> inline bool ReaderNormalizeOpacity(Settings& settings)
{
    auto normal=ReaderOpaqueExStyle(settings.exstyle),full=ReaderOpaqueExStyle(settings.fs_exstyle);
    bool changed=settings.alpha!=255 || normal!=settings.exstyle || full!=settings.fs_exstyle;
    settings.alpha=255;settings.exstyle=normal;settings.fs_exstyle=full;return changed;
}
