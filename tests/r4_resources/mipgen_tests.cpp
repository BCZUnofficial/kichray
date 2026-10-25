// P0-S5 coverage-preserving detail mips (xr_3da/KichR4MipGen.h).
#include "KichR4MipGen.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static void Put(std::vector<unsigned char>& d,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)d[at+i]=static_cast<unsigned char>(v>>(8*i));}
static std::vector<unsigned char> Header(uint32_t w,uint32_t h,bool rgba,uint32_t fourcc){
    std::vector<unsigned char> d(128,0);Put(d,0,0x20534444);Put(d,4,124);Put(d,8,0x1007);Put(d,12,h);Put(d,16,w);Put(d,28,1);Put(d,76,32);
    if(rgba){Put(d,80,0x41);Put(d,88,32);Put(d,92,0xff);Put(d,96,0xff00);Put(d,100,0xff0000);Put(d,104,0xff000000u);}
    else{Put(d,80,4);Put(d,84,fourcc);}
    Put(d,108,0x1000);return d;
}
int main(){try{
    // Grass-like atlas: green opaque blades 4 px wide every 16 px on black transparent.
    const uint32_t w=64,h=32;std::vector<unsigned char> dds=Header(w,h,true,0);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const bool blade=(x%16)<4;
        const unsigned char texel[4]={0,static_cast<unsigned char>(blade?200:0),0,static_cast<unsigned char>(blade?255:0)};dds.insert(dds.end(),texel,texel+4);}
    std::vector<unsigned char> out;
    Require(kich_r4_mipgen::Expand(dds.data(),uint32_t(dds.size()),200,out),"single-level RGBA atlas expands");
    kich_r4_dds::Image image={};Require(kich_r4_dds::Parse(out.data(),uint32_t(out.size()),16384,image),"generated DDS parses");
    Require(image.count==7 && image.format==kich_r4_dds::RGBA && image.width==64 && image.height==32,"full 64x32 chain to 1x1, RGBA8");
    uint32_t lw=w,lh=h;
    for(uint32_t i=0;i<image.count;++i){const kich_r4_dds::Mip& m=image.mips[i];size_t pass=0,n=m.bytes/4;double green=0;size_t opaque=0;
        for(size_t p=0;p<n;++p){const unsigned char* t=m.data+p*4;if(t[3]>=200){++pass;green+=t[1];++opaque;}}
        const double coverage=double(pass)/n;
        // A plain box chain fades the blades to alpha 128 by level 3 and 64 by
        // level 4 (no texel passes 200/255: the grass vanishes). Levels >= 8 px keep 25%.
        Require(coverage>0,"grass never vanishes down the chain");
        if(std::min(lw,lh)>=8)Require(std::fabs(coverage-.25)<=.05,"alpha-test coverage stays at level 0's 25%");
        if(opaque)Require(green/opaque>=190,"alpha-weighted colour: blades stay green, no dark fringe");
        lw=lw>1?lw/2:1;lh=lh>1?lh/2:1;}
    // Already mipped or tiny sources are left alone.
    std::vector<unsigned char> twice;Require(!kich_r4_mipgen::Expand(out.data(),uint32_t(out.size()),200,twice) && twice.empty(),"mipped source is not re-expanded");
    // BC1: c0 red, c1 blue, indices 0/1/2/3 per row; BC3 alpha a0=255,a1=0, index 0/1.
    unsigned char bc1[8]={0x00,0xF8,0x1F,0x00,0x00,0x55,0xAA,0xFF};unsigned char texel[16][4];
    kich_r4_mipgen::DecodeColor(bc1,true,texel);
    Require(texel[0][0]==255 && texel[0][2]==0 && texel[4][0]==0 && texel[4][2]==255 && texel[8][0]==170 && texel[12][0]==85,"BC1 four-colour decode");
    unsigned char bc3[8]={255,0,0x48,0x92,0x24,0x49,0x92,0x24};kich_r4_mipgen::DecodeAlphaBC3(bc3,texel);
    Require(texel[0][3]==255 && texel[1][3]==0,"BC3 alpha endpoints");
    // A real BC3 atlas (8x8) expands through the block decoder.
    std::vector<unsigned char> block=Header(8,8,false,kich_r4_dds::FourCC('D','X','T','5'));
    for(unsigned b=0;b<4;++b){const unsigned char data[16]={255,0,0,0,0,0,0,0,0x00,0x07,0x00,0x00,0,0,0,0};block.insert(block.end(),data,data+16);}
    Require(kich_r4_mipgen::Expand(block.data(),uint32_t(block.size()),200,out) && kich_r4_dds::Parse(out.data(),uint32_t(out.size()),16384,image) && image.count==4,"BC3 source expands to 4 levels");
    Require(image.mips[0].data[3]==255 && image.mips[3].data[3]>=200,"opaque BC3 stays opaque down the chain");
    std::printf("P0-S5 coverage-preserving mips: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
