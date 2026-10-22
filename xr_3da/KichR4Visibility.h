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
inline bool SunEnabled(const KichR4LightingFrame& lighting){
    const kich_r4_scene::Vec3 dir=kich_r4_scene::Normalize(kich_r4_scene::Vec3{lighting.sun_direction[0],lighting.sun_direction[1],lighting.sun_direction[2]});
    return kich_r4_scene::Dot(dir,dir)>.5f && (lighting.sun_color[0]>0 || lighting.sun_color[1]>0 || lighting.sun_color[2]>0);
}
inline void SelectShadowLights(const KichR4LightingFrame& lighting,bool native,bool (&selected)[8]){
    for(unsigned i=0;i<8;++i)selected[i]=false;
    const unsigned count=std::min(lighting.count,8u);unsigned spots=0;
    for(unsigned i=0;i<count && spots<2;++i)if(lighting.lights[i].type==2 && (!native || (lighting.shadow_mask&(1u<<i)))){selected[i]=true;++spots;}
    if(spots<2)for(unsigned i=0;i<count;++i)if(lighting.lights[i].type==1 && (!native || (lighting.shadow_mask&(1u<<i)))){selected[i]=true;break;}
    if(!native)for(unsigned i=0;i<count;++i)selected[i]=true;
}
}
#endif
