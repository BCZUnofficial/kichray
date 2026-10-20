#ifndef KICH_R4_DDS_H
#define KICH_R4_DDS_H
/* Bounds-checked, allocation-free DDS reader shared by EXE budget checks and
 * the actual R4 uploader. Native API constants never enter this header. */
#include <stdint.h>
#include <string.h>
namespace kich_r4_dds {
enum Format {RGBA=101,BGRA=102,BGRX=103,R8=104,RGBX=105,BC1=111,BC2=112,BC3=113,BC4=114,BC5=115,BC7=117};
struct Mip {const unsigned char* data;uint32_t row_pitch,bytes;};
struct Image {uint32_t width,height,format,count,bytes;Mip mips[15];};
inline uint32_t U32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint32_t FourCC(char a,char b,char c,char d){return uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24);}
inline bool Parse(const void* data,uint32_t bytes,uint32_t max_dimension,Image& result){
    if(!data || bytes<128 || bytes>64u*1024u*1024u || !max_dimension || max_dimension>16384)return false;
    const unsigned char* p=static_cast<const unsigned char*>(data);
    if(U32(p)!=FourCC('D','D','S',' ') || U32(p+4)!=124 || U32(p+76)!=32)return false;
    uint32_t w=U32(p+16),h=U32(p+12),count=U32(p+28),offset=128,format=0;
    if(!w || !h || w>16384 || h>16384 || U32(p+24)>1 || (U32(p+112)&0x0020fe00u))return false;
    if(!count)count=1;
    uint32_t possible=1;for(uint32_t n=w>h?w:h;n>1;n>>=1)++possible;
    if(count>possible || count>15)return false;
    const uint32_t flags=U32(p+80),code=U32(p+84),bits=U32(p+88);
    if(flags&4u){
        if(code==FourCC('D','X','T','1'))format=BC1;
        else if(code==FourCC('D','X','T','3'))format=BC2;
        else if(code==FourCC('D','X','T','5'))format=BC3;
        else if(code==FourCC('A','T','I','1') || code==FourCC('B','C','4','U'))format=BC4;
        else if(code==FourCC('A','T','I','2') || code==FourCC('B','C','5','U'))format=BC5;
        else if(code==FourCC('D','X','1','0')){
            if(bytes<148 || U32(p+132)!=3 || U32(p+140)!=1 || (U32(p+136)&4u))return false;
            offset=148;const uint32_t dx=U32(p+128);
            if(dx==28 || dx==29)format=RGBA;else if(dx==87 || dx==91)format=BGRA;
            else if(dx==88 || dx==93)format=BGRX;else if(dx==61)format=R8;
            else if(dx==71 || dx==72)format=BC1;else if(dx==74 || dx==75)format=BC2;
            else if(dx==77 || dx==78)format=BC3;else if(dx==80)format=BC4;
            else if(dx==83)format=BC5;else if(dx==98 || dx==99)format=BC7;
        }
    }else if((flags&0x40u) && bits==32 && U32(p+96)==0x0000ff00u){
        const uint32_t red=U32(p+92),blue=U32(p+100),alpha=U32(p+104);
        if(alpha!=0 && alpha!=0xff000000u)return false;
        if(red==0xffu && blue==0xff0000u)format=alpha?RGBA:RGBX;
        if(red==0xff0000u && blue==0xffu)format=alpha?BGRA:BGRX;
    }else if((flags&0x20000u) && bits==8 && U32(p+92)==0xffu)format=R8;
    if(!format)return false;
    Image image={};image.format=format;
    const bool block=format>=BC1;
    if(block && ((w%4 && w>4) || (h%4 && h>4)))return false;
    if(!block && (U32(p+8)&8u) && U32(p+20)!=w*(format==R8?1u:4u))return false;
    for(uint32_t i=0;i<count;++i){
        const uint32_t unit=block?((format==BC1 || format==BC4)?8u:16u):(format==R8?1u:4u);
        const uint32_t row=(block?(w+3)/4:w)*unit,rows=block?(h+3)/4:h;
        const uint64_t size=uint64_t(row)*rows;
        if(size>bytes-offset)return false;
        if(w<=max_dimension && h<=max_dimension){
            if(!image.count){image.width=w;image.height=h;}
            Mip& mip=image.mips[image.count++];mip.data=p+offset;mip.row_pitch=row;mip.bytes=static_cast<uint32_t>(size);
            image.bytes+=mip.bytes;
        }
        offset+=static_cast<uint32_t>(size);w=w>1?w/2:1;h=h>1?h/2:1;
    }
    if(!image.count)return false;
    result=image;return true;
}
inline void ToRGBA(uint32_t format,const unsigned char* source,unsigned char* out){
    if(format==R8){out[0]=out[1]=out[2]=source[0];out[3]=255;return;}
    const bool bgr=format==BGRA || format==BGRX;
    out[0]=source[bgr?2:0];out[1]=source[1];out[2]=source[bgr?0:2];
    out[3]=(format==BGRX || format==RGBX)?255:source[3];
}
}
#endif
