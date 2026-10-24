// Production HLSL PCF/helpers are inserted by run_shadow_tests.py. The depth
// oracle below is an independent analytic plane, not a copy of the PCF code.
#include "../../xr_3da/KichR4LightingMath.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <algorithm>
using std::isfinite;
float max(float a,float b){return std::max(a,b);}
using std::abs;
float saturate(float a){return std::max(0.f,std::min(1.f,a));}
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){} };
struct float3 {float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
struct float4 {float x,y,z,w;float4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){} };
float2 operator+(float2 a,float2 b){return float2(a.x+b.x,a.y+b.y);}
float2 operator+(float2 a,float b){return a+float2(b,b);}
float2 operator-(float2 a,float2 b){return float2(a.x-b.x,a.y-b.y);}
float2 operator*(float2 a,float b){return float2(a.x*b,a.y*b);}
float2 operator/(float2 a,float b){return a*(1.f/b);}
float3 operator-(float3 a,float3 b){return float3(a.x-b.x,a.y-b.y,a.z-b.z);}
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float3 operator/(float3 a,float b){return a*(1.f/b);}
bool operator<(float2 a,float b){return a.x<b || a.y<b;}
bool operator>(float2 a,float b){return a.x>b || a.y>b;}
bool any(bool v){return v;}
bool all(bool v){return v;}
bool isfinite(float2 v){return std::isfinite(v.x) && std::isfinite(v.y);}
float2 floor(float2 v){return float2(std::floor(v.x),std::floor(v.y));}
float dot(float2 a,float2 b){return a.x*b.x+a.y*b.y;}
float3 xyz(float4 v){return float3(v.x,v.y,v.z);}
float4 screen;int shadow_surface=0;
unsigned checks=0,comparisons=0;
void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
struct Texture2D {
    double a,b,c;bool edge;
    float SampleCmpLevelZero(int,float2 uv,float compare)const{
        ++comparisons;
        Require(std::fabs(uv.x*screen.w-.5-std::round(uv.x*screen.w-.5))<1e-4,"point comparison must hit an exact texel centre");
        Require(uv.x>=0 && uv.x<=1 && uv.y>=0 && uv.y<=1,"comparison must not read the white border");
        const double stored=edge?(uv.x<.5?.4:.6):a*uv.x+b*uv.y+c;
        return double(compare)<=stored?1.f:0.f;
    }
};
// PRODUCTION_SHADOW_HELPERS
float4 Transform(float3 p,float w,const kich_r4_scene::Matrix& m){
    return float4(p.x*m.m[0]+p.y*m.m[4]+p.z*m.m[8]+w*m.m[12],
        p.x*m.m[1]+p.y*m.m[5]+p.z*m.m[9]+w*m.m[13],
        p.x*m.m[2]+p.y*m.m[6]+p.z*m.m[10]+w*m.m[14],
        p.x*m.m[3]+p.y*m.m[7]+p.z*m.m[11]+w*m.m[15]);
}
void PlaneCases(){using namespace kich_r4_lighting;
    for(unsigned size:{16u,256u,512u,2048u})for(float degrees:{0.f,35.f,60.f,80.f})for(bool perspective:{false,true}){
        const float sx=std::tan(degrees*3.14159265f/180),sy=sx*.27f,d=10;
        Matrix projection=Identity();Texture2D surface={};
        if(perspective){KichR4LocalLight light={};light.type=1;light.range=40;projection=LocalShadow(light,4,size);
            const double A=projection.m[10],B=projection.m[14],s=projection.m[0];
            surface={-2*B*sx/(s*d),2*B*sy/(s*d),A+B/d*(1+sx/s-sy/s),false};
        }else{const float camera[3]={0,0,0},direction[3]={0,0,1};projection=SunShadow(camera,direction,512,size);
            surface={sx*512/1024.,-sy*512/1024.,(512+d-sx*256+sy*256)/1024.,false};}
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,.01f*sy);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        for(float x:{-.01f,0.f,.012f}){
            float3 position(x,.017f,d+sx*x+sy*.017f);
            const float precision=perspective?.003f:.01f;
            if(perspective){const float length=std::sqrt(position.x*position.x+position.y*position.y+position.z*position.z);position=position*(1-precision/length);}
            else position.z-=precision;
            const float4 projected=Transform(position,1,projection);screen.w=float(size);comparisons=0;
            const float lit=PCF(surface,projected,dx,dy);
            Require(lit>.9999f,"inclined receiver must not shadow itself across the full filter footprint");
            Require(comparisons==9,"ordinary pixel retains exactly nine comparisons");
            // A parallel blocker 2 cm from the receiver must still shadow it,
            // including the grazing 80 degree plane. No large bias may erase it.
            const double blocker_d=d-.02*std::sqrt(1+sx*sx+sy*sy);Texture2D blocker=surface;
            if(perspective){const double A=projection.m[10],B=projection.m[14],s=projection.m[0];
                blocker={-2*B*sx/(s*blocker_d),2*B*sy/(s*blocker_d),A+B/blocker_d*(1+sx/s-sy/s),false};}
            else blocker.c+=(blocker_d-d)/1024.;
            Require(PCF(blocker,projected,dx,dy)<.0001f,"nearby contact blocker must remain shadowed");
        }
    }
}
void Continuity(){
    screen.w=512;Texture2D edge={0,0,0,true};float previous=-1;
    for(unsigned step=0;step<=2000;++step){const float u=.495f+.01f*step/2000;
        const float current=PCF(edge,float4(u*2-1,0,.5f,1),float4(),float4());
        Require(current>=0 && current<=1,"normalized filter stays in visibility range");
        if(previous>=0)Require(std::fabs(current-previous)<.004f,"visibility must remain continuous across texel/window boundaries");
        previous=current;
    }
    for(unsigned size:{16u,256u,512u})for(float u:{0.f,.001f,.5f,.999f,1.f}){
        screen.w=float(size);Texture2D shadow={0,0,.4,false},light={0,0,.6,false};
        Require(PCF(shadow,float4(u*2-1,0,.5,1),float4(),float4())==0,"map edges must not introduce a lit border");
        Require(std::fabs(PCF(light,float4(u*2-1,0,.5,1),float4(),float4())-1)<1e-6,"edge normalization preserves full light");
    }
}
void CubeGuard(){using namespace kich_r4_lighting;KichR4LocalLight light={};light.type=1;light.range=30;
    for(unsigned size:{16u,32u,64u,128u,256u,512u})for(unsigned face=0;face<6;++face){const Matrix m=LocalShadow(light,face,size);
        for(int first:{-1,1})for(int second:{-1,1}){float3 p(5*first,5*second,5);const unsigned axis=face/2;
            float values[3]={5.f*first,5.f*second,5};values[axis]=face%2?-5.f:5.f;
            if(axis==0){values[1]=5.f*first;values[2]=5.f*second;}if(axis==1){values[0]=5.f*first;values[2]=5.f*second;}
            p=float3(values[0],values[1],values[2]);const float4 q=Transform(p,1,m);
            const float u=(q.x/q.w*.5f+.5f)*size,v=(.5f-q.y/q.w*.5f)*size;
            Require(std::floor(u)-.5f>=0 && std::floor(u)+1.5f<size && std::floor(v)-.5f>=0 && std::floor(v)+1.5f<size,"every cube corner has a complete rendered PCF guard at the actual resolution");
        }
    }
}
int main(){try{
    PlaneCases();Continuity();CubeGuard();
    float2 gradient=ShadowGradient(float3(0,0,1),float3(0,0,2));Require(gradient.x==0 && gradient.y==0,"singular UV Jacobian fails conservatively");
    gradient=ShadowGradient(float3(1,0,std::numeric_limits<float>::infinity()),float3(0,1,1));Require(gradient.x==0 && gradient.y==0,"nonfinite slope fails conservatively");
    Require(ShadowPlaneStable(4,.01f) && !ShadowPlaneStable(4,10000),"foreground to background jump does not become a receiver plane");
    Require(!ShadowPlaneStable(1,std::numeric_limits<float>::infinity()),"nonfinite derivative rejected");
    std::printf("E1.5 production PCF plane/contact/continuity/cube guard: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
