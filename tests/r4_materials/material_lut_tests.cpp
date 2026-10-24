// P0-L3: KichR4MaterialLut.h must reproduce SoC r2_rendertarget.cpp s_material
// texel-for-texel and produce a DDS that the R4 uploader parses as RGBA8.
// MSVC: cl /nologo /EHsc /O2 /I xr_3da tests\r4_materials\material_lut_tests.cpp
#include "KichR4MaterialLut.h"
#include "KichR4Dds.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static int Fail(const char* what){std::printf("FAIL: %s\n",what);return 1;}
// xrCore _bitwise.h iFloor, verbatim.
static int iFloor(float x){
    int a=*(const int*)(&x);int exponent=(127+31)-((a>>23)&0xFF);
    int r=(((unsigned)(a)<<8)|(1U<<31))>>exponent;exponent+=31-127;
    {int imask=(!(((((1<<(exponent)))-1)>>8)&a));exponent-=(31-127)+32;exponent>>=31;a>>=31;r-=(imask&a);r&=exponent;r^=a;}
    return r;
}
static int clampr(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static float _max(float a,float b){return a>b?a:b;}
// r2_rendertarget.cpp material fill loop, verbatim except for the texture write.
static void R2Texel(unsigned slice,unsigned x,unsigned y,int& d,int& s){
    const unsigned TEX_material_LdotN=128,TEX_material_LdotH=256;const float EPS_S=0.0000001f;
    float ld=float(x)/float(TEX_material_LdotN-1);
    float ls=float(y)/float(TEX_material_LdotH-1)+EPS_S;
    ls*=powf(ld,1/32.f);
    float fd,fs;
    switch(slice){
    case 0:{fd=powf(ld,0.75f);fs=powf(ls,16.f)*.5f;}break;
    case 1:{fd=powf(ld,0.90f);fs=powf(ls,24.f);}break;
    case 2:{fd=ld;fs=powf(ls*1.01f,128.f);}break;
    case 3:{float s0=fabsf(1-fabsf(0.05f*sinf(33.f*ld)+ld-ls));
        float s1=fabsf(1-fabsf(0.05f*cosf(33.f*ld*ls)+ld-ls));
        float s2=fabsf(1-fabsf(ld-ls));
        fd=ld;fs=powf(_max(_max(s0,s1),s2),24.f);fs*=powf(ld,1/7.f);}break;
    default:fd=fs=0;
    }
    d=clampr(iFloor(fd*255.5f),0,255);s=clampr(iFloor(fs*255.5f),0,255);
    if((y==(TEX_material_LdotH-1)) && (x==(TEX_material_LdotN-1))){d=255;s=255;}
}
int main(){
    using namespace kich_r4_material_lut;
    unsigned checked=0;
    for(unsigned slice=0;slice<Slices;++slice)for(unsigned y=0;y<LdotH;++y)for(unsigned x=0;x<LdotN;++x){
        int d,s;R2Texel(slice,x,y,d,s);unsigned char od,os;Texel(slice,x,y,od,os);
        if(d!=od || s!=os){std::printf("slice=%u x=%u y=%u r2=%d/%d r4=%u/%u\n",slice,x,y,d,s,od,os);return Fail("texel differs from R2");}
        ++checked;}
    const std::vector<unsigned char> dds=Dds();
    kich_r4_dds::Image image;
    if(!kich_r4_dds::Parse(dds.data(),static_cast<uint32_t>(dds.size()),1024,image))return Fail("R4 DDS parser rejects the table");
    if(image.format!=kich_r4_dds::RGBA || image.width!=Width || image.height!=Height || image.count!=1 || image.faces!=1)return Fail("unexpected DDS layout");
    // Spot-check the stacked layout: slice 2 row 37 texel 91 at RGBA row slice*256+y.
    unsigned char d,s;Texel(2,91,37,d,s);const unsigned char* p=image.mips[0].data+((2*LdotH+37)*Width+91)*4;
    if(p[0]!=d || p[1]!=d || p[2]!=d || p[3]!=s)return Fail("stacked slice layout");
    if(image.mips[0].data[((3*LdotH+255)*Width+127)*4+3]!=255)return Fail("forced last texel");
    std::printf("PASS: %u s_material texels identical to SoC R2 (iFloor/clampr), RGBA8 DDS %ux%u parsed\n",checked,image.width,image.height);
    return 0;
}
