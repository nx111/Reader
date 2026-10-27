#include "TitleBarTheme.h"
#include <cassert>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

struct Attribute {DWORD name,value;};
static std::vector<Attribute> attributes;
static LONG_PTR style=0;
static int directoryMode=0,apiVersion=2,rejectAttribute=0,loads=0,lookups=0;
static bool libraryMissing=false,exportMissing=false,highContrast=false,queryFailed=false;
static HMODULE module=reinterpret_cast<HMODULE>(10);
static HWND window=reinterpret_cast<HWND>(20);
const DWORD colorDefault=0xffffffffu;
const HRESULT invalid=static_cast<HRESULT>(0x80070057u);
static HRESULT SetAttribute(HWND target,DWORD name,LPCVOID value,DWORD bytes)
{
    assert(target==window&&bytes==4&&(name==20||name==35||name==36));
    DWORD color;std::memcpy(&color,value,4);attributes.push_back({name,color});
    if(apiVersion==0||(apiVersion==1&&name!=20)) return invalid;
    if(static_cast<int>(name)==rejectAttribute&&(name==20||color!=colorDefault)) return invalid;
    return 0;
}
UINT GetSystemDirectoryW(wchar_t* path,UINT count)
{
    assert(count==MAX_PATH);if(directoryMode==1)return 0;if(directoryMode==2)return MAX_PATH+1;
    std::wcscpy(path,L"C:\\Windows\\System32");return static_cast<UINT>(std::wcslen(path));
}
HMODULE LoadLibraryW(const wchar_t* path)
{assert(std::wstring(path)==L"C:\\Windows\\System32\\dwmapi.dll");++loads;return libraryMissing?nullptr:module;}
FARPROC GetProcAddress(HMODULE loaded,const char* name)
{assert(loaded==module&&!std::strcmp(name,"DwmSetWindowAttribute"));++lookups;return exportMissing?nullptr:reinterpret_cast<FARPROC>(SetAttribute);}
BOOL FreeLibrary(HMODULE loaded) {assert(loaded==module);return 1;}
LONG_PTR GetWindowLongPtrW(HWND target,int index) {assert(target==window&&index==GWL_STYLE);return style;}
BOOL SystemParametersInfoW(UINT action,UINT bytes,void* value,UINT flags)
{
    assert(action==SPI_GETHIGHCONTRAST&&bytes==sizeof(HIGHCONTRASTW)&&!flags);
    auto high=static_cast<HIGHCONTRASTW*>(value);assert(high->cbSize==sizeof(*high));
    high->dwFlags=highContrast?HCF_HIGHCONTRASTON:0;return !queryFailed;
}
static void Expect(DWORD mode,DWORD background,DWORD text)
{
    assert(attributes.size()==3);
    assert(attributes[0].name==20&&attributes[0].value==mode);
    assert(attributes[1].name==35&&attributes[1].value==background);
    assert(attributes[2].name==36&&attributes[2].value==text);
    attributes.clear();
}
int main(int argc,char** argv)
{
    assert(argc==2);std::string test=argv[1];
    if(test=="colors")
    {
        for(int dark:{0,1,0,1})
        {
            auto palette=MakeReaderPalette(dark);
            for(LONG_PTR chrome:{0L,0x00c00000L}) // Captionless/fullscreen and restored native caption.
            {
                style=chrome;ApplyNativeTitleBarTheme(window,palette,dark!=0);
                Expect(dark,palette.window,palette.text);assert(style==chrome);
                assert(ReaderColorContrast(palette.window,palette.text)>4.5);
            }
        }
        assert(loads==1&&lookups==1);
        style=WS_CHILD;ApplyNativeTitleBarTheme(window,MakeReaderPalette(1),true);
        ApplyNativeTitleBarTheme(nullptr,MakeReaderPalette(1),true);assert(attributes.empty());
    }
    else if(test=="contrast")
    {
        auto palette=MakeReaderPalette(1);
        ApplyNativeTitleBarTheme(window,palette,true);Expect(1,palette.window,palette.text);
        highContrast=true;ApplyNativeTitleBarTheme(window,palette,true);Expect(0,colorDefault,colorDefault);
        highContrast=false;queryFailed=true;ApplyNativeTitleBarTheme(window,palette,true);Expect(0,colorDefault,colorDefault);
        queryFailed=false;ApplyNativeTitleBarTheme(window,palette,true);Expect(1,palette.window,palette.text);
    }
    else if(test=="fallback")
    {
        auto palette=MakeReaderPalette(1);
        for(int failed:{35,36})
        {
            rejectAttribute=failed;ApplyNativeTitleBarTheme(window,palette,true);
            assert(attributes.size()==5&&attributes[3].name==35&&attributes[3].value==colorDefault);
            assert(attributes[4].name==36&&attributes[4].value==colorDefault);attributes.clear();
        }
        rejectAttribute=20;ApplyNativeTitleBarTheme(window,palette,true);Expect(1,colorDefault,colorDefault);
        rejectAttribute=0;apiVersion=1;ApplyNativeTitleBarTheme(window,palette,true);
        assert(attributes.size()==5&&attributes[0].value==1&&attributes[3].value==colorDefault&&attributes[4].value==colorDefault);attributes.clear();
        apiVersion=0;ApplyNativeTitleBarTheme(window,palette,true);Expect(1,colorDefault,colorDefault);
        apiVersion=2;ApplyNativeTitleBarTheme(window,palette,true);Expect(1,palette.window,palette.text);
    }
    else
    {
        if(test=="directory_error")directoryMode=1;
        else if(test=="directory_long")directoryMode=2;
        else if(test=="missing_library")libraryMissing=true;
        else if(test=="missing_export")exportMissing=true;
        else assert(false);
        for(int i=0;i<3;++i)ApplyNativeTitleBarTheme(window,MakeReaderPalette(i%2),i%2!=0);
        assert(attributes.empty()&&loads<=1&&lookups<=1);
    }
}
