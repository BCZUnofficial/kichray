#ifndef KICH_R4_VISIBILITY_H
#define KICH_R4_VISIBILITY_H
#include "KichR4LightingMath.h"
#include <algorithm>
#include <cmath>
namespace kich_r4_visibility {
using kich_r4_scene::Matrix;
struct Bounds {float min[3],max[3];bool valid;Bounds():valid(false){for(unsigned i=0;i<3;++i)min[i]=max[i]=0;}};
inline bool Visible(const float* low,const float* high,const Matrix& vp){
    if(!low || !high)return true;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(low[a]) || !kich_r4_lighting::Finite(high[a]) || low[a]>high[a])return true;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return true;
    unsigned outside[6]={};
    for(unsigned corner=0;corner<8;++corner){const float p[3]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};float c[4];
        for(unsigned a=0;a<4;++a)c[a]=p[0]*vp.m[a]+p[1]*vp.m[4+a]+p[2]*vp.m[8+a]+vp.m[12+a];
        for(unsigned a=0;a<4;++a)if(!kich_r4_lighting::Finite(c[a]))return true;
        /* D3D clip interval: -w<=x,y<=w and 0<=z<=w. Padding avoids
         * rejecting a caster exactly on a shadow/camera plane. */
        const float e=1.e-4f*(1.f+std::fabs(c[3]));
        outside[0]+=c[0]<-c[3]-e;outside[1]+=c[0]>c[3]+e;
        outside[2]+=c[1]<-c[3]-e;outside[3]+=c[1]>c[3]+e;
        outside[4]+=c[2]<-e;outside[5]+=c[2]>c[3]+e;}
    for(unsigned plane=0;plane<6;++plane)if(outside[plane]==8)return false;
    return true;
}
inline bool Visible(const Bounds& b,const Matrix& vp){return !b.valid || Visible(b.min,b.max,vp);}
inline bool VisibleSphere(const float* center,float radius,const Matrix& vp){
    if(!center || !kich_r4_lighting::Finite(radius) || radius<0)return true;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(center[a]))return true;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return true;
    // Row-vector D3D clip planes: columns w +/- x/y, z, and w - z.
    for(unsigned plane=0;plane<6;++plane){float p[4];
        const unsigned column=plane<4?plane/2:2;const float sign=(plane&1)?-1.f:1.f;
        for(unsigned a=0;a<4;++a)p[a]=plane==4?vp.m[a*4+2]:vp.m[a*4+3]+sign*vp.m[a*4+column];
        const float length=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
        if(length<1.e-12f)continue; // malformed/degenerate plane cannot reject
        const float distance=p[0]*center[0]+p[1]*center[1]+p[2]*center[2]+p[3];
        if(distance<-(radius+1.e-4f)*length)return false;
    }
    return true;
}
// Conservative normalized viewport rectangle. Used to bound light shading;
// its corners enclose the sphere, so it never trims valid illumination.
struct ScreenRect {
    float left,top,right,bottom;
    ScreenRect():left(0),top(0),right(1),bottom(1){}
};
inline ScreenRect LightScreenRect(const KichR4LocalLight& light,const Matrix& vp){
    ScreenRect full;
    if(!kich_r4_lighting::Finite(light.range) || light.range<0)return full;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(light.position[a]))return full;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return full;
    float low[2]={1.e20f,1.e20f},high[2]={-1.e20f,-1.e20f};
    for(unsigned corner=0;corner<8;++corner){float p[3],clip[4];
        for(unsigned a=0;a<3;++a)p[a]=light.position[a]+((corner&(1u<<a))?light.range:-light.range);
        for(unsigned a=0;a<4;++a)clip[a]=p[0]*vp.m[a]+p[1]*vp.m[4+a]+p[2]*vp.m[8+a]+vp.m[12+a];
        for(unsigned a=0;a<4;++a)if(!kich_r4_lighting::Finite(clip[a]))return full;
        // Projecting a box across the near plane is unbounded. Fall open for
        // camera-inside lights and behind-camera/invalid caller descriptors.
        if(clip[3]<=1.e-5f || clip[2]<=1.e-5f)return full;
        const float uv[2]={.5f+.5f*clip[0]/clip[3],.5f-.5f*clip[1]/clip[3]};
        for(unsigned a=0;a<2;++a){low[a]=std::min(low[a],uv[a]);high[a]=std::max(high[a],uv[a]);}
    }
    ScreenRect result;const float pad=1.e-4f;
    result.left=std::max(0.f,std::min(1.f,low[0]-pad));result.right=std::max(0.f,std::min(1.f,high[0]+pad));
    result.top=std::max(0.f,std::min(1.f,low[1]-pad));result.bottom=std::max(0.f,std::min(1.f,high[1]+pad));
    return result;
}
inline bool SunEnabled(const KichR4LightingFrame& lighting){
    const kich_r4_scene::Vec3 dir=kich_r4_scene::Normalize(kich_r4_scene::Vec3{lighting.sun_direction[0],lighting.sun_direction[1],lighting.sun_direction[2]});
    return kich_r4_scene::Dot(dir,dir)>.5f && (lighting.sun_color[0]>0 || lighting.sun_color[1]>0 || lighting.sun_color[2]>0);
}
inline void SelectShadowLights(const KichR4LightingFrame& lighting,bool native,std::vector<uint8_t>& selected){
    selected.assign(lighting.count,0);
    for(unsigned i=0;i<lighting.count && i<lighting.lights.size();++i)
        selected[i]=(!native || lighting.CastsShadow(i))?1:0;
}
}
#endif
