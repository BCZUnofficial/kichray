// P0-C2: production bloom HLSL (inserted by run_bloom_tests.py/.ps1) against
// the texcoords R2 phase_bloom interpolates from its D3D9 XYZRHW quads, and
// KichR4Bloom.h weights against a verbatim copy of R2 CalcGauss_k7/_wave.
#include "KichR4Bloom.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){}};
struct float3 {float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}};
struct float4 {float x,y,z,w;float4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}
    float4(float3 v,float a):x(v.x),y(v.y),z(v.z),w(a){}
    float3 xyz3()const{return float3(x,y,z);}
    float& operator[](int i){return (&x)[i];}
    float4 xxxx()const{return float4(x,x,x,x);}};
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float dot(float4 a,float b){return (a.x+a.y+a.z+a.w)*b;}
float lerp(float a,float b,float t){return a+(b-a)*t;}
struct int3{int3(int,int,int){}};
float2 operator+(float2 a,float2 b){return float2(a.x+b.x,a.y+b.y);}
float2 operator-(float2 a,float2 b){return float2(a.x-b.x,a.y-b.y);}
float2 operator*(float2 a,float b){return float2(a.x*b,a.y*b);}
float2 operator/(float2 a,float b){return float2(a.x/b,a.y/b);}
float2 operator/(float b,float2 a){return float2(b/a.x,b/a.y);}
float2 operator+(float2 a,float b){return float2(a.x+b,a.y+b);}
float3 operator+(float3 a,float3 b){return float3(a.x+b.x,a.y+b.y,a.z+b.z);}
float3 operator/(float3 a,float b){return float3(a.x/b,a.y/b,a.z/b);}
float4 operator+(float4 a,float4 b){return float4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);}
float4& operator+=(float4& a,float4 b){a=a+b;return a;}
float4 operator*(float a,float4 b){return float4(a*b.x,a*b.y,a*b.z,a*b.w);}
float4 operator/(float4 a,float b){return float4(a.x/b,a.y/b,a.z/b,a.w/b);}
float3& operator*=(float3& a,float b){a.x*=b;a.y*=b;a.z*=b;return a;}
float2 floor(float2 v){return float2(std::floor(v.x),std::floor(v.y));}
float dot(float3 a,float b){return (a.x+a.y+a.z)*b;}
float min(float a,float b){return std::min(a,b);}float max(float a,float b){return std::max(a,b);}
float2 xy(float4 v){return float2(v.x,v.y);}float3 xyz(float4 v){return float3(v.x,v.y,v.z);}
struct Varying{float4 p;};
float4 screen,light_position,light_direction,light_color;
struct Sampler{} surface;
struct Texture{std::vector<float2> uv;float4 value;
    float4 SampleLevel(Sampler,float2 at,int){uv.push_back(at);return value;}
    float4 Load(int3){return value;}} image0,image1;
// PRODUCTION_BLOOM
static int failures=0;
static void Near(double a,double b,const char* what){if(!(std::fabs(a-b)<=2e-6)){std::printf("FAIL: %s got %.9g expected %.9g\n",what,a,b);++failures;}}
// r2_rendertarget_phase_bloom.cpp, verbatim.
static void CalcGauss_k7(float* w0,float* w1,float r=3.3f,float s_out=1.f){
    float W[8];float mag=0;
    for(int i=-7;i<=0;i++)W[-i]=expf(-float(i*i)/(2*r*r));
    for(int i=0;i<8;i++)mag+=i?2*W[i]:W[i];
    for(int i=0;i<8;i++)W[i]=s_out*W[i]/mag;
    w0[0]=W[1];w0[1]=W[2];w0[2]=W[3];w0[3]=W[4];w1[0]=W[5];w1[1]=W[6];w1[2]=W[7];w1[3]=W[0];
}
static void CalcGauss_wave(float* w0,float* w1,float r_base,float r_detail,float s_out){
    float t0[4],t1[4];CalcGauss_k7(w0,w1,r_base,s_out);CalcGauss_k7(t0,t1,r_detail,s_out);
    for(int i=0;i<4;++i){w0[i]+=t0[i];w1[i]+=t1[i];}
}
int main(){
    const float W=1920,H=1080;screen=float4(W,H,0,0);
    // Weights: same floats as R2 for both axes.
    for(float kernel:{1.f,3.3f,7.f})for(float scale:{.5f,1.f,2.f}){
        float a0[4],a1[4],b0[4],b1[4];
        kich_r4_bloom::FilterWeights(false,kernel,scale,1920,1080,a0,a1);CalcGauss_wave(b0,b1,kernel,kernel/3.f,scale);
        for(int i=0;i<4;++i){Near(a0[i],b0[i],"x weight0");Near(a1[i],b1[i],"x weight1");}
        const float kv=kernel*1080.f/1920.f;
        kich_r4_bloom::FilterWeights(true,kernel,scale,1920,1080,a0,a1);CalcGauss_wave(b0,b1,kv,kv/3.f,scale);
        for(int i=0;i<4;++i){Near(a0[i],b0[i],"y weight0");Near(a1[i],b1[i],"y weight1");}}
    for(int x:{0,1,77,255})for(int y:{0,3,128,255}){
        // D3D9 pixel (x,y) of a 256^2 target; D3D11 SV_Position is its centre.
        Varying v;v.p=float4(x+.5f,y+.5f,0,1);const float px=x/256.f,py=y/256.f;
        // Build: a_0=half screen texel, one=(1/w)*((w/2)/256).
        image0.uv.clear();image0.value=float4(1,2,3,0);light_position=float4(.3f,0,0,0);
        float4 built=PSBloomBuild(v);
        const float ax=.5f/W,ay=.5f/H,ox=(1/W)*((W/2)/256),oy=(1/H)*((H/2)/256);
        const float ref[4][2]={{ax,ay},{ax+ox,ay},{ax,ay+oy},{ax+ox,ay+oy}};
        for(int k=0;k<4;++k){Near(image0.uv[k].x,ref[k][0]+px,"build u");Near(image0.uv[k].y,ref[k][1]+py,"build v");}
        Near(built.x,2,"build rgb=sum/2");Near(built.w,12-.3f,"build alpha=dot(rgb,1)-threshold");
        // KichRay mode 1: alpha from the unscaled sum, rgb scaled by strength.
        light_position=float4(.3f,1,.75f,.5f);built=PSBloomBuild(v);
        Near(built.x,1.5f,"mode1 strength");Near(built.w,12-.3f,"mode1 alpha above knee");
        // Gauss filter X and Y: R2 a_k from phase_bloom plus x/256.
        for(int vertical=0;vertical<2;++vertical){
            image0.uv.clear();image0.value=float4(1,1,1,1);
            light_position=float4(1,1,1,1);light_direction=float4(1,1,1,1);light_color=float4(vertical?0.f:1.f,vertical?1.f:0.f,0,0);
            float4 sum=PSBloomFilter(v);Near(sum.x,15,"15 weighted taps");
            const float half=.5f/256,one=1/256.f,two=2/256.f;
            float lo=half-one-half,hi=half+one+half; // a_1: -1 and +2 texel-units (x/256 grid)
            Near(image0.uv[0].x,half+px,"centre u");Near(image0.uv[0].y,half+py,"centre v");
            for(int k=1;k<=7;++k){
                // C++ evaluates the two operands of A+B in unspecified order.
                float2 minus=image0.uv[2*k-1],plus=image0.uv[2*k];
                if(minus.x+minus.y>plus.x+plus.y)std::swap(minus,plus);
                if(!vertical){Near(minus.x,lo+px,"x minus u");Near(minus.y,half+py,"x minus v");Near(plus.x,hi+px,"x plus u");Near(plus.y,half+py,"x plus v");}
                else{Near(minus.x,half+px,"y minus u");Near(minus.y,lo+py,"y minus v");Near(plus.x,half+px,"y plus u");Near(plus.y,hi+py,"y plus v");}
                lo-=two;hi+=two;}}
        // Fast filter: p0 = half texel, +-kernel_b texels, (-,-),(+,+),(+,-),(-,+).
        image0.uv.clear();light_position=float4(.7f,0,0,0);PSBloomFilterFast(v);
        const float d=.7f/256,c=.5f/256;const float fr[4][2]={{-d,-d},{d,d},{d,-d},{-d,d}};
        for(int k=0;k<4;++k){bool found=false; // operand order of the sum is unspecified
            for(int j=0;j<4;++j)found=found || (std::fabs(image0.uv[j].x-(c+fr[k][0]+px))<=2e-6 && std::fabs(image0.uv[j].y-(c+fr[k][1]+py))<=2e-6);
            Near(found?1:0,1,"fast tap");}
    }
    // P0-C1 phase_luminance texcoords: pass 0 a_k + x/64, passes 1/2 a[k] + x/_ts.
    for(int x:{0,5,63})for(int y:{0,9,63}){
        Varying v;v.p=float4(x+.5f,y+.5f,0,1);image0.uv.clear();image0.value=float4(1,1,1,0);
        float4 lum=PSLuminance1(v);Near(lum.x,(.3f+.48f+.22f)*8,"luminance*def_hdr");
        const float a[4][2]={{1,1},{3,1},{1,3},{3,3}};
        for(int k=0;k<4;++k){bool found=false; // constructor argument order is unspecified
            for(int j=0;j<4;++j)found=found || (std::fabs(image0.uv[j].x-(a[k][0]/256+x/64.f))<=2e-6 && std::fabs(image0.uv[j].y-(a[k][1]/256+y/64.f))<=2e-6);
            Near(found?1:0,1,"lum1 tap");}}
    for(int pass=0;pass<2;++pass){const float src=pass?8.f:64.f;const int limit=pass?1:8;
        for(int x=0;x<limit;x+=5)for(int y=0;y<limit;y+=3){
            Varying v;v.p=float4(x+.5f,y+.5f,0,1);image0.uv.clear();image0.value=float4(.25f,.5f,.75f,1);
            float4 out=pass?float4():PSLuminance2(v);
            if(!pass)for(int i=0;i<4;++i)Near(out[i],.625f,"lum2 quarter means");
            if(pass){image1.value=float4(.5f,0,0,0);light_position=float4(1,0,1,.1f);out=PSLuminance3(v);
                Near(out.x,.5f+(1-.5f)*.1f,"tonemap off: lerp(prev, 1, adapt)");
                image0.uv.clear();light_position=float4(.25f,1,.001f,1);out=PSLuminance3(v);
                Near(out.x,.25f/(.625f+.001f),"tonemap on: middlegray/(lum+lowlum)");}
            for(int k=0;k<16;++k){ // r2_rendertarget_phase_luminance.cpp kernel
                const float ax=float((k*2+1)%8)/src,ay=float((k/4)*2+1)/src;
                Near(image0.uv[k].x,ax+x/(src/8),"lum u");Near(image0.uv[k].y,ay+y/(src/8),"lum v");}}}
    // kich_graphics_math.h soft knee below threshold.
    Near(KichBloomAlpha(.25f,.3f,.5f),(.25f-.3f+.15f)*(.25f-.3f+.15f)/(4*.15f),"soft knee");
    Near(KichBloomAlpha(0,.3f,.5f),0,"no bloom far below threshold");
    if(failures)return 1;
    std::printf("PASS: R2 bloom texcoords (build, gauss X/Y, fast), KichRay alpha, CalcGauss_wave weights, luminance 64/8/1 kernels and exposure scale\n");return 0;
}
