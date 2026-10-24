// Production shader functions are inserted by run_sun_tests.py. The independent
// oracle uses the Lambert material slice: dynamic sunlight follows current
// visibility and ignores source sun alpha; deferred indirect is combine hmodel,
// forward indirect follows source hemi; R2 static sun uses source alpha*2.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
#include <vector>
#include "KichR4MaterialLut.h"
using std::abs;using std::floor;using std::fmod;using std::pow;using std::sin;using std::cos;
float max(float a,float b){return std::max(a,b);}
int min(int a,int b){return std::min(a,b);}
float clamp(float a,float lo,float hi){return std::max(lo,std::min(hi,a));}
float saturate(float a){return clamp(a,0,1);}
float frac(float a){return a-std::floor(a);}
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){} };
struct float3 {
    float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    void operator/=(float a){x/=a;y/=a;z/=a;}
};
struct float4 {
    union{float x;float r;};union{float y;float g;};union{float z;float b;};union{float w;float a;};
    float4(float v=0):x(v),y(v),z(v),w(v){}
    float4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    float4(float3 v,float a):x(v.x),y(v.y),z(v.z),w(a){}
};
struct int2{int x,y;explicit int2(float2 v):x(int(v.x)),y(int(v.y)){} };
struct int3{int x,y,z;int3(int2 v,int a):x(v.x),y(v.y),z(a){} };
float2 operator+(float2 a,float2 b){return float2(a.x+b.x,a.y+b.y);}
float2 operator-(float2 a,float b){return float2(a.x-b,a.y-b);}
float2 operator/(float2 a,float2 b){return float2(a.x/b.x,a.y/b.y);}
float2 clamp(float2 v,float2 lo,float2 hi){return float2(clamp(v.x,lo.x,hi.x),clamp(v.y,lo.y,hi.y));}
float3 operator+(float3 a,float3 b){return float3(a.x+b.x,a.y+b.y,a.z+b.z);}
float3 operator-(float3 a,float3 b){return float3(a.x-b.x,a.y-b.y,a.z-b.z);}
float3 operator-(float3 a){return float3(-a.x,-a.y,-a.z);}
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float3 operator*(float3 a,float3 b){return float3(a.x*b.x,a.y*b.y,a.z*b.z);}
float4 operator+(float4 a,float4 b){return float4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);}
float4 operator*(float4 a,float b){return float4(a.x*b,a.y*b,a.z*b,a.w*b);}
float2 lerp(float2 a,float2 b,float t){return float2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);}
float3 lerp(float3 a,float3 b,float t){return a+(b-a)*t;}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float dot(float4 a,float4 b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}
float length(float3 a){return std::sqrt(dot(a,a));}
float2 xy(float4 a){return float2(a.x,a.y);}
float3 xyz(float4 a){return float3(a.x,a.y,a.z);}
float3 splat3(float a){return float3(a,a,a);}
struct Varying{float4 p,n,c;float3 w;};
struct Texture2D {
    float4 value;unsigned loads=0;
    float4 Load(int3){++loads;return value;}
}image0,image1,shadow2;
// P0-L3 R2 s_material at t10: bilinear CLAMP SampleLevel over the generated table.
struct LutSampler{} material_lut_surface;
struct LutTexture {
    std::vector<unsigned char> texels;
    float Texel(int x,int y,int c){x=std::min(std::max(x,0),127);y=std::min(std::max(y,0),1023);return texels[128+(y*128+x)*4+c]/255.f;}
    float4 SampleLevel(LutSampler,float2 uv,int){
        if(texels.empty())texels=kich_r4_material_lut::Dds();
        const float px=uv.x*128-.5f,py=uv.y*1024-.5f;const int x0=int(std::floor(px)),y0=int(std::floor(py));const float fx=px-x0,fy=py-y0;
        float out[4];for(int c=0;c<4;++c)out[c]=(Texel(x0,y0,c)*(1-fx)+Texel(x0+1,y0,c)*fx)*(1-fy)+(Texel(x0,y0+1,c)*(1-fx)+Texel(x0+1,y0+1,c)*fx)*fy;
        return float4(out[0],out[1],out[2],out[3]);}
}material_lut;
float4 camera,screen,sun_direction,sun_color,ambient,hemi,material,fog,fog_params,fog_plane,env_ambient(0,0,0,1);
float visibility=1,surface_gloss=0;
float3 surface_normal(0,1,0);
float3 WorldPosition(float2,float){return float3(0,0,0);}
float3 LoadReceiverPosition(float2,float){return -xyz(camera);}
void ShadowDerivatives(float3,float3& dx,float3& dy){dx=float3(.01f,0,0);dy=float3(0,0,.01f);}
// Same gate as production SunVisibility: only sun_color.w==1 uses shadow maps.
float SunVisibility(float3,float3,float3){return sun_color.w<=0 || sun_color.w>1.5f?0:visibility;}
float3 DecodeNormal(float2){return surface_normal;}
float3 Normal(Varying,bool){return surface_normal;}
float4 Base(Varying v){return v.c;}
float Gloss(Varying){return surface_gloss;}
// PRODUCTION_SUN_HELPERS
unsigned checks=0;
void Near(float value,float expected,const char* message){
    ++checks;if(!std::isfinite(value) || std::fabs(value-expected)>2e-6f)throw std::runtime_error(message);
}
void Check(float4 value,float3 diffuse,float specular,const char* message){
    Near(value.x,diffuse.x,message);Near(value.y,diffuse.y,message);
    Near(value.z,diffuse.z,message);Near(value.w,specular,message);
}
int main(){try{
    camera=float4(0,3,0,1);sun_direction=float4(0,-1,0,0);sun_color=float4(1,1,1,1);
    ambient=float4(.07f,.11f,.17f,0);hemi=float4(.3f,.2f,.1f,0);material.w=2;
    image1.value=float4(.5f,0,0,0);
    Varying receiver={};receiver.p=float4(10,20,.5f,1);receiver.c=float4(.2f,.4f,.7f,.6f);
    // These alphas cover source/lightmap masks and a zero ROS-sun value. Their
    // original values remain available in the GBuffer diagnostic channel.
    for(float mask:{0.f,.125f,.25f,.5f,1.f})for(float h:{0.f,.37f,1.f})
    for(float shadow:{0.f,.25f,1.f})for(float gloss:{0.f,1.f}){
        shadow2.value=float4(.2f,.4f,.7f,mask);visibility=shadow;surface_gloss=gloss;
        image0.value=float4(0,0,h,PackMaterial(gloss,2));receiver.n.w=h;
        // Forward hemi response is the R2 table's diffuse at (h,0) for the Phong slice.
        const float3 indirect=xyz(ambient)+xyz(hemi)*MaterialResponse(h,0,2).x;
        Check(PSSun(receiver),splat3(shadow),gloss*shadow,
            "dynamic sun must follow current shadow visibility, independently of baked/ROS sun alpha; no indirect");
        // Forward keeps R2 forward L_ambient+hemi and the same direct sun.
        Check(PSForwardSun(receiver,true),xyz(receiver.c)*(indirect+splat3(shadow))+splat3(gloss*shadow),receiver.c.w,
            "forward light/alpha must retain its previous unmasked response");
        Near(shadow2.value.a,mask,"source sun diagnostic alpha must be retained");
        // R2 USE_R2_STATIC_SUN: no maps, source occlusion*2 owns direct sun.
        sun_color.w=2;
        Check(PSSun(receiver),splat3(mask*2),gloss*mask*2,"static sun must use source sun alpha*2, not shadow maps");
        Check(PSForwardSun(receiver,true),xyz(receiver.c)*(indirect+splat3(1))+splat3(gloss),receiver.c.w,
            "forward static sun has no G-buffer occlusion and stays unoccluded");
        sun_color.w=0;
        Check(PSSun(receiver),float3(),0,"disabled sun adds no direct light");
        sun_color.w=1;
    }
    // An unlit normal receives no direct sun; indirect is not written here.
    surface_normal=float3(0,-1,0);image0.value=float4(0,0,.37f,PackMaterial(1,2));visibility=1;
    Check(PSSun(receiver),float3(),0,"sun direction/material response must remain active");
    // Sky rejection and the raw shadow diagnostic must remain independent of
    // indirect illumination and the receiver's baked alpha.
    image1.value.r=1;Check(PSSun(receiver),float3(),0,"sky cannot receive surface lighting");
    image1.value.r=.5f;screen.z=8;visibility=.25f;
    Check(PSSun(receiver),splat3(.25f),0,"shadow diagnostic must show current map visibility");
    std::printf("P0-L1 production sun lighting: %u checks passed (baked/ROS mask independence, current shadows, static/disabled sun, forward hemi, sky)\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
