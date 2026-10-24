// Production shader functions are inserted by run_sun_tests.py. The independent
// oracle uses the Lambert material slice: sunlight follows current visibility,
// indirect follows source hemi, and source static-sun alpha cannot change either.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
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
float4 camera,screen,sun_direction,sun_color,ambient,hemi,material,fog,fog_params,fog_plane;
float visibility=1,surface_gloss=0;
float3 surface_normal(0,1,0);
float3 WorldPosition(float2,float){return float3(0,0,0);}
float3 RelativePosition(float2,float){return -xyz(camera);}
void ShadowDerivatives(float3,float3& dx,float3& dy){dx=float3(.01f,0,0);dy=float3(0,0,.01f);}
float SunVisibility(float3,float3,float3){return visibility;}
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
        const float3 indirect=xyz(ambient)+xyz(hemi)*h;
        const float3 expected=indirect+splat3(shadow);
        Check(PSSun(receiver),expected,gloss*shadow,
            "dynamic sun must follow current shadow visibility, independently of baked/ROS sun alpha");
        // Forward already used unit sun visibility multiplier; maintain parity
        // with deferred irradiance while preserving authored RGB and opacity.
        Check(PSForwardSun(receiver,true),xyz(receiver.c)*expected+splat3(gloss*shadow),receiver.c.w,
            "forward light/alpha must retain its previous unmasked response");
        Near(shadow2.value.a,mask,"source sun diagnostic alpha must be retained");
    }
    // An unlit normal retains exactly the existing ambient/hemi response.
    surface_normal=float3(0,-1,0);image0.value=float4(0,0,.37f,PackMaterial(1,2));visibility=1;
    Check(PSSun(receiver),xyz(ambient)+xyz(hemi)*.37f,0,"sun direction/material response must remain active");
    // Sky rejection and the raw shadow diagnostic must remain independent of
    // indirect illumination and the receiver's baked alpha.
    image1.value.r=1;Check(PSSun(receiver),float3(),0,"sky cannot receive surface lighting");
    image1.value.r=.5f;screen.z=8;visibility=.25f;
    Check(PSSun(receiver),splat3(.25f),0,"shadow diagnostic must show current map visibility");
    std::printf("E1.6 production sun lighting: %u checks passed (baked/ROS mask independence, current shadows, hemi, forward, sky)\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
