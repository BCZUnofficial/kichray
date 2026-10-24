#ifndef KICH_R4_OCCLUSION_H
#define KICH_R4_OCCLUSION_H
#include <algorithm>
#include <cmath>
#include <stdint.h>
namespace kich_r4_occlusion {
struct Rect {float left,top,right,bottom,depth;};
inline bool Finite(float value){return std::isfinite(value);}
// false means that a conservative screen-space test cannot be made. The
// caller must keep the complete light volume, never substitute its origin.
inline bool ProjectBox(const float* low,const float* high,const float* camera,const float* vp,Rect& out){
    if(!low || !high || !camera || !vp)return false;
    bool inside=true;
    for(unsigned a=0;a<3;++a){
        if(!Finite(low[a]) || !Finite(high[a]) || !Finite(camera[a]) || low[a]>high[a])return false;
        inside=inside && camera[a]>=low[a] && camera[a]<=high[a];
    }
    if(inside)return false;
    for(unsigned a=0;a<16;++a)if(!Finite(vp[a]))return false;
    out.left=out.top=out.depth=1.e30f;out.right=out.bottom=-1.e30f;
    for(unsigned corner=0;corner<8;++corner){float clip[4]={};
        const float p[3]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};
        for(unsigned a=0;a<4;++a){clip[a]=p[0]*vp[a]+p[1]*vp[4+a]+p[2]*vp[8+a]+vp[12+a];if(!Finite(clip[a]))return false;}
        // A clipped/behind-camera box has no bounded projection of its eight
        // original corners. R2 likewise fails open across the near plane.
        if(clip[3]<=1.e-5f || clip[2]<=1.e-5f)return false;
        const float x=.5f+.5f*clip[0]/clip[3],y=.5f-.5f*clip[1]/clip[3],z=clip[2]/clip[3];
        if(!Finite(x) || !Finite(y) || !Finite(z))return false;
        out.left=(std::min)(out.left,x);out.right=(std::max)(out.right,x);
        out.top=(std::min)(out.top,y);out.bottom=(std::max)(out.bottom,y);out.depth=(std::min)(out.depth,z);
    }
    if(out.depth>=1.f || out.right<0 || out.left>1 || out.bottom<0 || out.top>1)return false;
    // Two pixels at the donor's 64x64 HOM resolution avoid edge rounding and
    // thin openings turning a partially exposed light volume into a rejection.
    const float pad=2.f/64.f;
    out.left=(std::max)(0.f,out.left-pad);out.top=(std::max)(0.f,out.top-pad);
    out.right=(std::min)(1.f,out.right+pad);out.bottom=(std::min)(1.f,out.bottom+pad);
    out.depth=(std::max)(0.f,out.depth-1.e-5f);
    return true;
}
inline bool RangeBounds(const float* position,float range,float* low,float* high){
    if(!position || !low || !high || !Finite(range) || range<=0)return false;
    for(unsigned a=0;a<3;++a){low[a]=position[a]-range;high[a]=position[a]+range;
        if(!Finite(position[a]) || !Finite(low[a]) || !Finite(high[a]))return false;}
    return true;
}
}
#endif
