#ifndef KICH_R4_MIP_GEN_H
#define KICH_R4_MIP_GEN_H
/* P0-S5 coverage-preserving mip chains for alpha-tested atlases that ship with
 * one level (the level build_details grass/leaf atlas). Without mips distant
 * grass samples the full-size texture and shimmers; a plain box-filtered chain
 * fades alpha below the R2 detail reference (200/255) and the grass thins out.
 * Each level is built from the unscaled previous one with alpha-weighted
 * colour (no dark fringe from transparent texels), then its alpha is scaled so
 * the fraction of texels passing the alpha test matches level 0 (Castano,
 * "Computing Alpha Mipmaps", 2010). Output: RGBA8 DDS with the full chain. */
#include "KichR4Dds.h"
#include <vector>
#include <algorithm>
#include <stdexcept>
namespace kich_r4_mipgen {
inline void Color565(uint32_t c,int* out){out[0]=int((c>>11)&31)*255/31;out[1]=int((c>>5)&63)*255/63;out[2]=int(c&31)*255/31;}
// One 4x4 colour block (BC1 layout), alpha from the block for BC1 only.
inline void DecodeColor(const unsigned char* b,bool bc1,unsigned char out[16][4]){
    const uint32_t c0=b[0]|(b[1]<<8),c1=b[2]|(b[3]<<8);int p[4][4];
    Color565(c0,p[0]);Color565(c1,p[1]);p[0][3]=p[1][3]=255;
    for(unsigned k=0;k<3;++k){
        if(!bc1 || c0>c1){p[2][k]=(2*p[0][k]+p[1][k])/3;p[3][k]=(p[0][k]+2*p[1][k])/3;}
        else{p[2][k]=(p[0][k]+p[1][k])/2;p[3][k]=0;}}
    p[2][3]=255;p[3][3]=(!bc1 || c0>c1)?255:0;
    const uint32_t bits=kich_r4_dds::U32(b+4);
    for(unsigned i=0;i<16;++i){const int* c=p[(bits>>(2*i))&3];for(unsigned k=0;k<4;++k)out[i][k]=static_cast<unsigned char>(c[k]);}
}
inline void DecodeAlphaBC3(const unsigned char* b,unsigned char out[16][4]){
    int a[8];a[0]=b[0];a[1]=b[1];
    if(a[0]>a[1])for(int i=1;i<7;++i)a[i+1]=((7-i)*a[0]+i*a[1])/7;
    else{for(int i=1;i<5;++i)a[i+1]=((5-i)*a[0]+i*a[1])/5;a[6]=0;a[7]=255;}
    uint64_t bits=0;for(unsigned i=0;i<6;++i)bits|=uint64_t(b[2+i])<<(8*i);
    for(unsigned i=0;i<16;++i)out[i][3]=static_cast<unsigned char>(a[(bits>>(3*i))&7]);
}
inline void DecodeAlphaBC2(const unsigned char* b,unsigned char out[16][4]){
    for(unsigned i=0;i<16;++i){const unsigned v=(b[i/2]>>((i&1)*4))&15;out[i][3]=static_cast<unsigned char>(v*17);}
}
// Level 0 of a parsed image as tightly packed RGBA8.
inline bool Decode(const kich_r4_dds::Image& image,std::vector<unsigned char>& rgba){
    using namespace kich_r4_dds;const uint32_t w=image.width,h=image.height;const Mip& m=image.mips[0];
    rgba.assign(size_t(w)*h*4,0);
    if(image.format==BC1 || image.format==BC2 || image.format==BC3){
        const uint32_t unit=image.format==BC1?8:16,bw=(w+3)/4,bh=(h+3)/4;
        if(uint64_t(bw)*bh*unit>m.bytes)return false;
        for(uint32_t by=0;by<bh;++by)for(uint32_t bx=0;bx<bw;++bx){const unsigned char* b=m.data+(size_t(by)*bw+bx)*unit;unsigned char texel[16][4];
            if(image.format==BC1)DecodeColor(b,true,texel);
            else{DecodeColor(b+8,false,texel);if(image.format==BC2)DecodeAlphaBC2(b,texel);else DecodeAlphaBC3(b,texel);}
            for(unsigned i=0;i<16;++i){const uint32_t x=bx*4+(i&3),y=by*4+(i>>2);if(x<w && y<h)std::copy(texel[i],texel[i]+4,&rgba[(size_t(y)*w+x)*4]);}}
        return true;}
    if(image.format>=BC1)return false;
    const uint32_t pixel=image.format==R8 || image.format==A8?1u:image.format==RGB24?3u:image.format>=RGB565 && image.format<=ARGB4444?2u:4u;
    if(uint64_t(m.row_pitch)*h>m.bytes || m.row_pitch<w*pixel)return false;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)ToRGBA(image.format,m.data+size_t(y)*m.row_pitch+size_t(x)*pixel,&rgba[(size_t(y)*w+x)*4]);
    return true;
}
inline double Coverage(const std::vector<unsigned char>& rgba,unsigned reference,double scale){
    size_t passed=0;const size_t n=rgba.size()/4;
    for(size_t i=0;i<n;++i)if(std::min(255.0,rgba[i*4+3]*scale+.5)>=reference)++passed;
    return n?double(passed)/n:0;
}
// Half-size level from the unscaled previous one; colour weighted by alpha.
inline void Downsample(const std::vector<unsigned char>& source,uint32_t w,uint32_t h,std::vector<unsigned char>& out,uint32_t& ow,uint32_t& oh){
    ow=w>1?w/2:1;oh=h>1?h/2:1;out.assign(size_t(ow)*oh*4,0);
    for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x){double c[3]={0,0,0},plain[3]={0,0,0},alpha=0;unsigned taps=0;
        for(uint32_t j=0;j<2;++j)for(uint32_t i=0;i<2;++i){const uint32_t sx=std::min(w-1,x*2+i),sy=std::min(h-1,y*2+j);
            const unsigned char* t=&source[(size_t(sy)*w+sx)*4];const double a=t[3]/255.0;
            for(unsigned k=0;k<3;++k){c[k]+=t[k]*a;plain[k]+=t[k];}alpha+=t[3];++taps;}
        unsigned char* o=&out[(size_t(y)*ow+x)*4];
        for(unsigned k=0;k<3;++k){const double v=alpha>0?c[k]/(alpha/255.0):plain[k]/taps;o[k]=static_cast<unsigned char>(std::min(255.0,v+.5));}
        o[3]=static_cast<unsigned char>(alpha/taps+.5);}
}
inline void Put32(std::vector<unsigned char>& d,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)d[at+i]=static_cast<unsigned char>(v>>(8*i));}
// Returns false (out empty) unless the source is a single-level 2D texture.
inline bool Expand(const void* data,uint32_t bytes,unsigned alpha_reference,std::vector<unsigned char>& out){
    out.clear();kich_r4_dds::Image image={};
    if(!kich_r4_dds::Parse(data,bytes,16384,image) || image.count!=1 || image.faces!=1 || image.width<8 || image.height<8)return false;
    std::vector<unsigned char> level;if(!Decode(image,level))return false;
    uint32_t w=image.width,h=image.height,levels=1;for(uint32_t n=std::max(w,h);n>1;n>>=1)++levels;
    levels=std::min(levels,15u);
    const double target=Coverage(level,alpha_reference,1);
    size_t total=128;{uint32_t x=w,y=h;for(uint32_t i=0;i<levels;++i){total+=size_t(x)*y*4;x=x>1?x/2:1;y=y>1?y/2:1;}}
    out.assign(total,0);
    Put32(out,0,0x20534444);Put32(out,4,124);Put32(out,8,0x2100F);Put32(out,12,h);Put32(out,16,w);Put32(out,20,w*4);Put32(out,28,levels);
    Put32(out,76,32);Put32(out,80,0x41);Put32(out,88,32);Put32(out,92,0xff);Put32(out,96,0xff00);Put32(out,100,0xff0000);Put32(out,104,0xff000000u);
    Put32(out,108,0x401008);
    size_t at=128;std::vector<unsigned char> next;
    for(uint32_t i=0;i<levels;++i){
        double scale=1;
        if(i && target>0){double low=0,high=8;
            for(unsigned step=0;step<20;++step){const double mid=(low+high)*.5;if(Coverage(level,alpha_reference,mid)<target)low=mid;else high=mid;}
            scale=high;}
        for(size_t p=0;p<level.size();p+=4){out[at+p]=level[p];out[at+p+1]=level[p+1];out[at+p+2]=level[p+2];
            out[at+p+3]=static_cast<unsigned char>(std::min(255.0,level[p+3]*scale+.5));}
        at+=level.size();
        if(i+1<levels){uint32_t nw,nh;Downsample(level,w,h,next,nw,nh);level.swap(next);w=nw;h=nh;}
    }
    return true;
}
}
#endif
