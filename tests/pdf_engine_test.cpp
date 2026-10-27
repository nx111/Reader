#include "PdfEngine.h"
#include "PdfLayout.h"
#include <cassert>
#include <iostream>
#include <codecvt>
#include <locale>
#include <thread>

int main(int argc,char** argv)
{
    assert(argc==4);std::wstring_convert<std::codecvt_utf8<wchar_t>> wide;
    std::string action=argv[1];ReaderPdfDocument pdf;
    bool opened=pdf.Open(wide.from_bytes(argv[2]),wide.from_bytes(argv[3]));
    if(action=="invalid") {assert(!opened&&pdf.error==3);return 0;}
    if(action=="password") {assert(!opened&&pdf.error==4);return 0;}
    if(action=="missing") {assert(!opened&&pdf.error==1000);return 0;}
    if(action=="large") {assert(!opened&&pdf.error==1001);return 0;}
    assert(opened&&pdf.pages==2);assert(pdf.title==L"Reader PDF regression");assert(pdf.author==L"Reader tests");
    assert(pdf.outline.size()==2&&pdf.outline[0].page==0&&pdf.outline[1].page==1);
    std::atomic<unsigned> request{10};ReaderPdfFrame frame;frame.page=action=="scan"?1:0;
    auto size=pdf.Size(frame.page);assert(size.width==300&&size.height==600);
    auto g=ReaderPdfLayout(size.width,size.height,300,400,0,96,0,0);
    frame.width=g.width;frame.height=g.height;frame.pageWidth=g.pageWidth;frame.pageHeight=g.pageHeight;frame.left=g.left;frame.top=g.top;
    if(action=="cancel") {assert(!pdf.Render(frame,request,9)&&frame.pixels.empty());return 0;}
    if(action=="bounds") {frame.width=100000;assert(!pdf.Render(frame,request,10)&&frame.pixels.empty());return 0;}
    assert(pdf.Render(frame,request,10));assert(frame.pixels.size()==300*400*4);
    size_t nonWhite=0;for(size_t i=0;i<frame.pixels.size();i+=4) if(frame.pixels[i]<200||frame.pixels[i+1]<200||frame.pixels[i+2]<200) ++nonWhite;
    assert(nonWhite>100);
    if(action=="scan") {assert(frame.text.empty());assert(nonWhite>20000);}
    else {assert(frame.text.find(L"Searchable page")!=std::wstring::npos);assert(frame.characters.size()==frame.text.size());}
    if(action=="repeat")
    {
        for(int i=0;i<30;++i) {ReaderPdfFrame f=frame;f.page=i%2;assert(pdf.Render(f,request,10));}
        for(int i=0;i<20;++i) {ReaderPdfDocument next;assert(next.Open(wide.from_bytes(argv[2]),wide.from_bytes(argv[3])));next.Close();}
        std::thread a([&]{ReaderPdfDocument other;assert(other.Open(wide.from_bytes(argv[2]),wide.from_bytes(argv[3])));assert(other.Render(frame,request,10));});
        ReaderPdfFrame second=frame;assert(pdf.Render(second,request,10));a.join();
    }
    std::cout<<"PDF engine "<<action<<" passed\n";
}
