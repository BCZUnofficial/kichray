// P0-L3: production MaterialResponse/MaterialLutSlice are inserted by
// run_material_lut_tests.py (or .ps1). The oracle is an independent D3D9-style
// tex3D of the R2 128x256x4 volume: LINEAR, U/V CLAMP, W WRAP, z=(mtl+.5)/4.
#include "KichR4MaterialLut.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){}};
struct float4 {float r,g,b,a;};
float2 operator+(float2 a,float2 b){return float2(a.x+b.x,a.y+b.y);}
float2 operator-(float2 a,float b){return float2(a.x-b,a.y-b);}
float2 operator/(float2 a,float2 b){return float2(a.x/b.x,a.y/b.y);}
float2 operator*(float2 a,float b){return float2(a.x*b,a.y*b);}
float2 lerp(float2 a,float2 b,float t){return float2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);}
float2 clamp(float2 v,float2 lo,float2 hi){return float2(std::min(std::max(v.x,lo.x),hi.x),std::min(std::max(v.y,lo.y),hi.y));}
using std::floor;
static std::vector<unsigned char> table(kich_r4_material_lut::Width*kich_r4_material_lut::Height*4);
static float Texel(int x,int y,int channel){return table[(y*kich_r4_material_lut::Width+x)*4+channel]/255.f;}
// D3D11 SampleLevel, MIN_MAG_LINEAR, CLAMP (the bound s11 state).
struct Sampler{} material_lut_surface;
struct Texture{
    float4 SampleLevel(Sampler,float2 uv,int) const {
        const float px=uv.x*kich_r4_material_lut::Width-.5f,py=uv.y*kich_r4_material_lut::Height-.5f;
        const int x0=int(std::floor(px)),y0=int(std::floor(py));const float fx=px-x0,fy=py-y0;
        auto at=[](int x,int y,int c){x=std::min(std::max(x,0),kich_r4_material_lut::Width-1);y=std::min(std::max(y,0),kich_r4_material_lut::Height-1);return Texel(x,y,c);};
        float4 out={};float* o=&out.r;
        for(int c=0;c<4;++c)o[c]=(at(x0,y0,c)*(1-fx)+at(x0+1,y0,c)*fx)*(1-fy)+(at(x0,y0+1,c)*(1-fx)+at(x0+1,y0+1,c)*fx)*fy;
        return out;}
} material_lut;
// PRODUCTION_MATERIAL_LUT
// Independent oracle: R2 volume addressing, no knowledge of the stacked layout.
static float Volume(int x,int y,int z,int c){
    x=std::min(std::max(x,0),127);y=std::min(std::max(y,0),255);z=((z%4)+4)%4;
    unsigned char d,s;kich_r4_material_lut::Texel(unsigned(z),unsigned(x),unsigned(y),d,s);return (c?s:d)/255.f;}
static void Tex3D(float u,float v,float w,float& d,float& s){
    const float px=u*128-.5f,py=v*256-.5f,pz=w*4-.5f;
    const int x0=int(std::floor(px)),y0=int(std::floor(py)),z0=int(std::floor(pz));
    const float fx=px-x0,fy=py-y0,fz=pz-z0;float r[2]={};
    for(int c=0;c<2;++c)for(int k=0;k<2;++k){const float wz=k?fz:1-fz;
        for(int j=0;j<2;++j){const float wy=j?fy:1-fy;
            for(int i=0;i<2;++i){const float wx=i?fx:1-fx;r[c]+=wx*wy*wz*Volume(x0+i,y0+j,z0+k,c);}}}
    d=r[0];s=r[1];
}
int main(){
    table=kich_r4_material_lut::Dds();table.erase(table.begin(),table.begin()+128);
    unsigned checks=0;double worst=0;
    std::srand(12345);
    const float classes[]={0,.2f,.4f,1,1.6f,2,2.8f,3};
    for(int n=0;n<200000;++n){
        const float ld=-.2f+1.4f*float(std::rand())/RAND_MAX,lh=-.2f+1.4f*float(std::rand())/RAND_MAX;
        const float m=n<8?classes[n]:classes[std::rand()%8];
        const float2 got=MaterialResponse(ld,lh,m);float d,s;Tex3D(ld,lh,(m+.5f)/4,d,s);
        const double e=std::max(std::fabs(got.x-d),std::fabs(got.y-s));worst=std::max(worst,e);
        if(!(e<2e-5)){std::printf("FAIL: ld=%g lh=%g m=%g got=%g/%g r2=%g/%g\n",ld,lh,m,got.x,got.y,d,s);return 1;}
        if(got.x<0 || got.x>1 || got.y<0 || got.y>1){std::printf("FAIL: bounds\n");return 1;}
        ++checks;}
    // Exact texel centres reproduce stored 8-bit values; the forced corner is full.
    const float2 corner=MaterialResponse(1,1,2);
    if(corner.x!=1 || corner.y!=1){std::printf("FAIL: forced last texel\n");return 1;}
    const float2 dark=MaterialResponse(0,1,3);
    if(dark.x!=0 || dark.y!=0){std::printf("FAIL: zero L.N\n");return 1;}
    std::printf("PASS: production MaterialResponse equals R2 tex3D(s_material) for %u samples, max error %.3g\n",checks,worst);
    return 0;
}
