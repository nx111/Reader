#pragma once
#include <cstdint>
#include <initializer_list>
#include <cmath>

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
inline uint32_t ReaderBlendColor(uint32_t foreground,uint32_t background,unsigned alpha)
{
    alpha=alpha>255?255:alpha;unsigned result=0;
    for(unsigned shift:{0u,8u,16u}) result|=((((foreground>>shift)&255)*alpha+((background>>shift)&255)*(255-alpha)+127)/255)<<shift;
    return result;
}
inline double ReaderColorLight(uint32_t color)
{
    double light=0;const double weights[]={0.2126,0.7152,0.0722};
    for(int i=0;i<3;++i) {double c=((color>>(i*8))&255)/255.0;light+=weights[i]*(c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4));}
    return light;
}
inline double ReaderColorContrast(uint32_t a,uint32_t b)
{double x=ReaderColorLight(a),y=ReaderColorLight(b);return x>y?(x+0.05)/(y+0.05):(y+0.05)/(x+0.05);}
inline uint32_t ReaderReadableBlend(uint32_t ink,uint32_t paper,unsigned alpha,double contrast)
{
    for(;alpha<255;alpha=(alpha>247?255:alpha+8))
    {uint32_t color=ReaderBlendColor(ink,paper,alpha);if(ReaderColorContrast(color,paper)>=contrast) return color;}
    return ink;
}
// Opaque local paint only. The focus rail shares the reader's effective solid
// background and changes ink contrast without any window alpha or reparenting.
inline ReaderPalette ReaderFocusPalette(ReaderPalette p,uint32_t paper,bool clear,bool highContrast=false)
{
    if(highContrast) return p;
    uint32_t ink=p.text;
    if(ReaderColorContrast(ink,paper)<4.0)
    {uint32_t dark=ThemeRgb(16,17,20),light=ThemeRgb(246,247,249);ink=ReaderColorContrast(dark,paper)>ReaderColorContrast(light,paper)?dark:light;}
    p.window=p.surface=p.raised=paper;
    p.text=ReaderReadableBlend(ink,paper,clear?216:136,clear?4.0:3.0);
    p.muted=p.text;p.disabled=ReaderBlendColor(ink,paper,64);
    p.hover=ReaderBlendColor(ink,paper,clear?24:14);p.pressed=ReaderBlendColor(ink,paper,clear?36:22);
    p.border=ReaderBlendColor(ink,paper,clear?32:16);p.accent=p.text;
    p.selected=ReaderBlendColor(ink,paper,clear?40:24);
    p.selectedText=ReaderReadableBlend(ink,p.selected,clear?216:136,clear?4.0:3.0);
    return p;
}
struct ReaderScrollColors { uint32_t background,track,thumb,hot,pressed; };
inline ReaderScrollColors ReaderScrollbarColors(const ReaderPalette& p,uint32_t background)
{
    auto light=[](uint32_t c) {return (int)(((c&255)*299+((c>>8)&255)*587+((c>>16)&255)*114)/1000);};
    uint32_t ink=p.text;int difference=light(ink)-light(background);
    if(difference<96&&difference>-96) ink=light(background)<128?ThemeRgb(232,233,237):ThemeRgb(31,32,36);
    return {background,ReaderBlendColor(ink,background,10),ReaderBlendColor(ink,background,55),
        ReaderBlendColor(ink,background,76),ReaderBlendColor(ink,background,100)};
}
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
