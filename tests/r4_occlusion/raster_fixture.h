#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
typedef uint32_t u32;typedef int32_t s32;typedef int16_t s16;
typedef int BOOL;typedef u32* LPDWORD;typedef void* LPVOID;
#define TRUE 1
#define FALSE 0
#ifndef _WIN32
#define __stdcall
#endif
#define IC inline
inline int iFloor(float v){return static_cast<int>(std::floor(v));}
inline int iCeil(float v){return static_cast<int>(std::ceil(v));}
inline int _abs(int v){return std::abs(v);}
template<class T> inline void clamp(T& value,T low,T high){value=(std::max)(low,(std::min)(high,value));}
struct Fvector {float x,y,z;void set(float a,float b,float c){x=a;y=b;z=c;}};
struct Fplane {float x,y,z,d;};
class occTri;
struct MemoryFixture {
    void mem_fill32(void* dst,u32 value,u32 count){std::fill_n(static_cast<u32*>(dst),count,value);}
    // Production is Win32. Preserve the same all-pointer-zero operation when
    // running its rasterizer with 64-bit host pointers in this portable test.
    template<size_t N> void mem_fill32(occTri* (*dst)[N],u32 value,u32 count){assert(!value);std::fill_n(&dst[0][0],count,static_cast<occTri*>(NULL));}
};
struct RendererFixture {struct HOMFixture {unsigned syncs=0;void MT_SYNC(){++syncs;}} HOM;};
extern MemoryFixture Memory;
extern RendererFixture RImplementation;
