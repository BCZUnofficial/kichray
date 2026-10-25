// P0-S9 receiver plane from neighbouring pixels (ShadowDerivativesLoaded).
// The production HLSL helpers are inserted by run_receiver_plane_tests.ps1.
// The depth buffer below is an independent analytic ray cast of a road with
// a crease (two facets) and a wall in front of it, not production code.
#include "../../xr_3da/KichR4LightingMath.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <stdexcept>
using std::isfinite;
struct int2;
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){}explicit float2(const int2& v);};
struct int2 {int x,y;int2(int a=0,int b=0):x(a),y(b){}explicit int2(float2 v):x(int(v.x)),y(int(v.y)){}};
float2::float2(const int2& v):x(float(v.x)),y(float(v.y)){}
struct int3 {int x,y,z;int3(int2 a,int b):x(a.x),y(a.y),z(b){}};
struct float3 {float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}};
struct float4 {float x,y,z,w;float4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}float4(float2 a,float c,float d):x(a.x),y(a.y),z(c),w(d){}};
int2 operator+(int2 a,int2 b){return int2(a.x+b.x,a.y+b.y);}
int2 operator-(int2 a,int2 b){return int2(a.x-b.x,a.y-b.y);}
float2 operator+(float2 a,float b){return float2(a.x+b,a.y+b);}
float2 operator*(float2 a,float b){return float2(a.x*b,a.y*b);}
float3 operator-(float3 a,float3 b){return float3(a.x-b.x,a.y-b.y,a.z-b.z);}
float3 operator-(float3 a){return float3(-a.x,-a.y,-a.z);}
float3 operator+(float3 a,float3 b){return float3(a.x+b.x,a.y+b.y,a.z+b.z);}
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float3 operator/(float3 a,float b){return a*(1.f/b);}
bool any(bool v){return v;}bool all(bool v){return v;}
struct bool2 {bool x,y;operator bool()const{return x || y;}};
bool2 operator<(int2 a,int b){return {a.x<b,a.y<b};}
bool2 operator>=(int2 a,int2 b){return {a.x>=b.x,a.y>=b.y};}
bool isfinite(float3 v){return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);}
using std::abs;float min(float a,float b){return a<b?a:b;}float max(float a,float b){return a>b?a:b;}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float3 cross(float3 a,float3 b){return float3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
float3 xyz(float4 v){return float3(v.x,v.y,v.z);}
kich_r4_scene::Matrix receiver_inverse,inverse_vp;float4 screen,camera;
float4 mul(float4 p,const kich_r4_scene::Matrix& m){const float* a=m.m;
    return float4(p.x*a[0]+p.y*a[4]+p.z*a[8]+p.w*a[12],p.x*a[1]+p.y*a[5]+p.z*a[9]+p.w*a[13],p.x*a[2]+p.y*a[6]+p.z*a[10]+p.w*a[14],p.x*a[3]+p.y*a[7]+p.z*a[11]+p.w*a[15]);}
struct Texel {float r;};
struct DepthTexture {int width=0,height=0;std::vector<float> values;
    Texel Load(int3 p)const{if(p.x<0 || p.y<0 || p.x>=width || p.y>=height)return {0};return {values[size_t(p.y)*width+p.x]};}};
DepthTexture receiver_depth_texture,image1;
// PRODUCTION_RECEIVER_PLANE
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
// Analytic scene in camera-relative world space. Facet 0: road y=-2 (z<30);
// facet 1: past the crease it rises by `bend` degrees; facet 2: a wall at
// z=12 for x>3 (a depth discontinuity in front of the road).
struct Scene {float bend;
    float3 Normal(int facet)const{if(facet==2)return float3(0,0,-1);if(facet==0)return float3(0,1,0);
        const float a=bend*3.14159265f/180;return float3(0,std::cos(a),-std::sin(a));}
    // Hit distance along a ray with unit view depth, and the facet hit.
    bool Cast(float3 o,float3 d,float& t,int& facet)const{t=1e30f;facet=-1;
        const float a=bend*3.14159265f/180;
        if(d.y<0){const float g=(-2-o.y)/d.y;const float3 p=o+d*g;if(p.z<30){t=g;facet=0;}}
        {// rising facet: y=-2+(z-30)tan(a), z>=30
            const float k=std::tan(a),denominator=d.y-k*d.z;if(std::fabs(denominator)>1e-9f){const float g=(-2-k*30-o.y+k*o.z)/denominator;const float3 p=o+d*g;
                if(g>0 && p.z>=30 && g<t){t=g;facet=1;}}}
        if(d.z>0){const float g=(12-o.z)/d.z;const float3 p=o+d*g;if(p.x>3 && p.y>-2 && p.y<3 && g<t){t=g;facet=2;}}
        return facet>=0;}
};
int main(){try{
    const int W=320,H=200;screen=float4(float(W),float(H),0,0);
    // Camera 1.7 m up looking down the road (pitch -6 degrees), fx/fy focal scales.
    const float pitch=-6*3.14159265f/180,fx=1.2f,fy=1.2f*W/H;
    const float3 right(1,0,0),up(0,std::cos(pitch),-std::sin(pitch)),forward(0,std::sin(pitch),std::cos(pitch));
    // receiver_inverse: (ndc*w, w, 1) row vector -> camera-relative world.
    receiver_inverse=kich_r4_scene::Identity();float* m=receiver_inverse.m;
    m[0]=right.x/fx;m[1]=right.y/fx;m[2]=right.z/fx;m[4]=up.x/fy;m[5]=up.y/fy;m[6]=up.z/fy;m[8]=forward.x;m[9]=forward.y;m[10]=forward.z;m[12]=m[13]=m[14]=0;m[15]=1;
    for(float bend:{2.f,10.f,35.f}){const Scene scene={bend};
        receiver_depth_texture.width=image1.width=W;receiver_depth_texture.height=image1.height=H;
        receiver_depth_texture.values.assign(size_t(W)*H,0);image1.values.assign(size_t(W)*H,.5f);
        std::vector<int> facets(size_t(W)*H,-1);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){const float nx=(x+.5f)*2/W-1,ny=1-(y+.5f)*2/H;
            const float3 d=right*(nx/fx)+up*(ny/fy)+forward;float t;int facet;
            if(scene.Cast(float3(0,0,0),d,t,facet) && t<400){receiver_depth_texture.values[size_t(y)*W+x]=t;facets[size_t(y)*W+x]=facet;}}
        unsigned planes=0,kept=0,surfaces=0,slivers=0;
        for(int y=2;y<H-2;++y)for(int x=2;x<W-2;++x){const int facet=facets[size_t(y)*W+x];if(facet<0)continue;++surfaces;
            const float2 pixel(x+.5f,y+.5f);const float3 p=LoadReceiverPosition(pixel,.5f);
            float3 dx(0,0,0),dy(0,0,0);ShadowDerivativesLoaded(pixel,p,dx,dy);
            auto at=[&](int u,int v){return facets[size_t(v)*W+u];};
            // Expected: per axis one side whose two neighbours share the facet.
            const bool wide=((at(x-1,y)==facet && at(x-2,y)==facet) || (at(x+1,y)==facet && at(x+2,y)==facet)) &&
                ((at(x,y-1)==facet && at(x,y-2)==facet) || (at(x,y+1)==facet && at(x,y+2)==facet));
            if(dot(dx,dx)==0 && dot(dy,dy)==0){if(wide){++kept;std::printf("  kept x=%d y=%d facet=%d\n",x,y,facet);}else ++slivers;continue;}++planes;
            const float3 n=scene.Normal(facet);const float lx=std::sqrt(dot(dx,dx)),ly=std::sqrt(dot(dy,dy));
            Require(std::fabs(dot(n,dx))<=2e-3f*lx && std::fabs(dot(n,dy))<=2e-3f*ly,"neighbour plane lies in the pixel's own facet, also next to the crease and the wall");
            // The step is to a neighbour on the same facet (never across the crease or the wall edge).
            auto same=[&](const float3& step,int2 axis){for(int sign:{-1,1}){const int2 at(x+axis.x*sign,y+axis.y*sign);
                    const float3 q=LoadReceiverPosition(float2(at.x+.5f,at.y+.5f),.5f),e=sign>0?q-p:p-q,r=e-step;
                    if(dot(r,r)<=1e-8f*(1+dot(step,step)))return facets[size_t(at.y)*W+at.x]==facet;}
                return false;};
            Require(same(dx,int2(1,0)) && same(dy,int2(0,1)),"neighbour steps stay on the pixel's own facet");}
        // Every surface pixel gets its facet's plane: no fallback line along the crease or the wall foot.
        Require(kept==0,"every pixel with two same-facet neighbours per axis gets its plane: no line along the crease or the wall foot");
        Require(slivers<surfaces/200,"only thin slivers (perspective wall edge) keep the quad result");
        // Before P0-S9: a 2x2 quad (ddx_fine) spanning two facets lost its plane.
        unsigned straddling=0;
        for(int y=2;y<H-2;++y)for(int x=2;x<W-2;++x){const int facet=facets[size_t(y)*W+x];if(facet<0)continue;const int qx=x&~1,qy=y&~1;
            for(int k=0;k<4;++k)if(facets[size_t(qy+(k>>1))*W+qx+(k&1)]!=facet){++straddling;break;}}
        Require(straddling>slivers,"the fixture has quads astride the crease and the wall");
        std::printf("bend %4.1f: %u surface pixels, %u neighbour planes, %u one/two-pixel slivers keep the quad result (quads astride an edge before: %u pixels)\n",bend,surfaces,planes,slivers,straddling);
    }
    // A one-pixel feature: both sides break its plane, the quad result stays.
    {receiver_depth_texture.values.assign(size_t(W)*H,20.f);for(int y=0;y<H;++y)receiver_depth_texture.values[size_t(y)*W+100]=5.f;
        const float2 pixel(100.5f,100.5f);float3 dx(1,2,3),dy(4,5,6);ShadowDerivativesLoaded(pixel,LoadReceiverPosition(pixel,.5f),dx,dy);
        Require(dx.x==1 && dy.z==6,"a one-pixel feature keeps the quad derivatives");}
    // Legacy producers without receiver_inverse keep the quad derivatives.
    {receiver_inverse.m[15]=0;float3 dx(1,2,3),dy(4,5,6);ShadowDerivativesLoaded(float2(50.5f,50.5f),float3(0,0,1),dx,dy);
        Require(dx.y==2 && dy.x==4,"no receiver inverse: quad derivatives");}
    std::printf("P0-S9 receiver plane from neighbours: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
