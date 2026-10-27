#pragma once
#include <algorithm>
#include <cstdint>

inline int64_t ReaderScrollMaximum(int minimum,int maximum,unsigned page)
{ return (std::max)((int64_t)minimum,(int64_t)maximum-(page?(int64_t)page-1:0)); }
inline bool ReaderScrollHasRange(int minimum,int maximum,unsigned page)
{ return maximum>=minimum && ReaderScrollMaximum(minimum,maximum,page)>minimum; }
inline int ReaderScrollbarThickness(int available,int dpi)
{return (std::max)(0,(std::min)(available,6*(std::max)(96,dpi)/96));}
struct ReaderScrollbarThumb
{
    int start=0,end=0;
    bool Contains(int point) const {return point>=start && point<end;}
};
// Keep native thumb coordinates: native tracking, RTL and accessibility use the
// same geometry; only its perpendicular paint thickness is reduced.
inline ReaderScrollbarThumb ReaderScrollbarNativeThumb(int length,int start,int end)
{
    ReaderScrollbarThumb result;
    result.start=(std::max)(0,(std::min)(length,start));
    result.end=(std::max)(result.start,(std::min)(length,end));return result;
}
class ReaderScrollbarVisibility
{
    uint64_t fadeAt=0;
public:
    unsigned opacity=0;
    bool pointer=false,thumb=false,drag=false;
    void Reset() {opacity=0;pointer=thumb=drag=false;fadeAt=0;}
    void Pointer(bool inside,bool overThumb,uint64_t now)
    {
        bool left=pointer&&!inside;pointer=inside;thumb=inside&&overThumb;
        if(pointer || drag) {opacity=255;fadeAt=0;}
        else if(left) fadeAt=now+250;
    }
    void Drag(bool value,uint64_t now)
    {
        bool ended=drag&&!value;drag=value;
        if(drag) {opacity=255;fadeAt=0;}
        else if(ended&&!pointer) fadeAt=now+250;
    }
    bool Animate() const {return opacity && !pointer && !drag && fadeAt;}
    void Tick(uint64_t now)
    {
        if(pointer || drag) {opacity=255;return;}
        if(!fadeAt || now<=fadeAt) return;
        opacity=now-fadeAt>=180?0:(unsigned)((180-(now-fadeAt))*255/180);
        if(!opacity) fadeAt=0;
    }
};
