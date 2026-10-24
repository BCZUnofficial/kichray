// Production fog/combine functions are inserted by run_fog_shader_tests.py.
// Texture loads and world reconstruction are controlled inputs; no GPU pixels
// are claimed. The oracle is the current R2 combine formula and blend factors.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){}};
struct float3 {float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}};
struct float4 {
    union{float x;float r;};union{float y;float g;};union{float z;float b;};union{float w;float a;};
    float4(float v=0):x(v),y(v),z(v),w(v){}float4(float3 v,float a):x(v.x),y(v.y),z(v.z),w(a){}
    float4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
};
struct int2 {int x,y;explicit int2(float2 v):x(int(v.x)),y(int(v.y)){}};
struct int3 {int x,y,z;int3(int2 v,int a):x(v.x),y(v.y),z(a){}};
float3 operator+(float3 a,float3 b){return float3(a.x+b.x,a.y+b.y,a.z+b.z);}
float3 operator+(float3 a,float b){return float3(a.x+b,a.y+b,a.z+b);}
float3 operator-(float3 a,float3 b){return float3(a.x-b.x,a.y-b.y,a.z-b.z);}
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float3 operator*(float3 a,float3 b){return float3(a.x*b.x,a.y*b.y,a.z*b.z);}
float3 lerp(float3 a,float3 b,float t){return a*(1-t)+b*t;}
float saturate(float x){return std::max(0.f,std::min(1.f,x));}
float length(float3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
float dot(float4 a,float4 b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}
float2 xy(float4 a){return float2(a.x,a.y);}float3 xyz(float4 a){return float3(a.x,a.y,a.z);}
float3 splat3(float a){return float3(a,a,a);}
struct Texture {float4 value;float4 Load(int3){return value;}} image0,image1,shadow0,shadow1;
struct Varying {float4 p;};struct Discard {};
float4 fog,fog_params,fog_plane,camera,screen,alpha;
float3 relative_position;
float3 RelativePosition(float2,float){return relative_position;}
float3 WorldPosition(float2,float){return relative_position+xyz(camera);}
float3 DecodeNormal(float2){return float3(0,1,0);}
// PRODUCTION_FOG_HELPERS
unsigned checks=0;
void Near(float a,float b,const char* why){++checks;if(!std::isfinite(a) || std::fabs(a-b)>2e-6f)throw std::runtime_error(why);}
void Near(float3 a,float3 b,const char* why){Near(a.x,b.x,why);Near(a.y,b.y,why);Near(a.z,b.z,why);}
int main(){try{
    Varying fragment={};fragment.p=float4(10,20,.5f,1);camera=float4(100,4,-200,150);
    image0.value=float4(.2f,.6f,.8f,.4f);image1.value=float4(.5f,.9f,1.3f,.15f);shadow0.value=float4(.5f,.5f,.37f,.2f);shadow1.value=float4(.5f);
    fog=float4(.15f,.25f,.3f,0);fog_params=float4(-.2f,.01f,0,1);alpha.w=20;
    const float3 lit=xyz(image0.value)*xyz(image1.value)+splat3(image1.value.a),sky(.4f,.5f,.7f);
    for(float distance:{0.f,20.f,45.f,70.f,120.f,250.f}){
        relative_position=float3(0,0,distance);const float fraction=saturate((distance-20)/100);
        const float4 combined=PSCombine(fragment);const float3 fogged=lit*(1-fraction)+xyz(fog)*fraction;
        Near(xyz(combined),fogged,"production combine uses radial R2 fogged color");
        Near(combined.a,fraction*fraction,"production combine uses R2 quadratic sky visibility");
        Near(xyz(combined)*(1-combined.a)+sky*combined.a,fogged*(1-fraction*fraction)+sky*(fraction*fraction),"inverse-alpha/source-alpha blend approaches full sky at far plane");
    }
    relative_position=float3(0,0,200);screen.z=2;
    const float4 normal_view=PSCombine(fragment);Near(xyz(normal_view),float3(.5f,1,.5f),"debug normals remain unfogged at far distance");Near(normal_view.a,1,"debug has opaque alpha");
    screen.z=0;shadow1.value.r=1;bool discarded=false;try{PSCombine(fragment);}catch(const Discard&){discarded=true;}
    if(!discarded)throw std::runtime_error("native empty depth preserves actual drawn sky");
    ++checks;
    shadow1.value.r=.5f;fog_params=float4();relative_position=float3(0,0,200);
    const float4 disabled=PSCombine(fragment);Near(xyz(disabled),lit,"absent weather range leaves lighting unchanged");Near(disabled.a,0,"absent weather range does not reveal sky through geometry");
    fog_plane=float4(0,0,-.01f,1.2f);fog_params.w=1;
    const float3 point(0,0,70),color(.7f,.3f,.1f);
    Near(FogTransmittance(point),1,"draw opt-out leaves HUD/UI/effects unchanged");
    Near(ForwardFog(color,FogTransmittance(point)),color,"disabled forward fog preserves color");
    fog_params.z=1;Near(FogTransmittance(point),.5f,"opted-in world draw uses source R2 view-plane fog");
    Near(ForwardFog(color,FogTransmittance(point)),color*.5f+xyz(fog)*.5f,"ordinary forward base receives exactly one fog color");
    fog_params.z=2;Near(ForwardFog(color,FogTransmittance(point)),color*.5f,"additive base attenuates without duplicate fog color");
    fog_params.w=0;Near(FogTransmittance(point),1,"invalid forward plane has no effect");
    std::printf("E1.8 production fog/combine shader: %u checks passed (R2 color/sky math, exclusions, additive fog; no GPU rasterization)\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
