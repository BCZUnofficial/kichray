#ifndef KICH_R4_LOCAL_SHADOW_PLAN_H
#define KICH_R4_LOCAL_SHADOW_PLAN_H
/* P0-S7 local light shadow plan, the R2 render_lights equivalent:
 *  - a face is rendered only if its pyramid can hold a visible receiver
 *    (camera frustum, HUD box around the camera) and is not hidden by HOM;
 *    R2 tests each omnipart volume with an occlusion query;
 *  - each light gets its own map size from the R2 compute_xf_spot factors
 *    (screen coverage, intensity, light size, frustum width), as a power of
 *    two with hysteresis so walking past a class boundary never flickers;
 *  - the sizes fit the shared byte budget by halving the largest first. */
#include "KichR4Lighting.h"
#include "KichR4LightingMath.h"
#include "KichR4ShadowPoolPolicy.h"
#include <math.h>
#include <vector>
namespace kich_r4_local_shadow {
using kich_r4_scene::Vec3;using kich_r4_scene::Matrix;
// Optional current-frame occlusion test of a world box (native: R2 HOM).
typedef bool (*BoxVisibility)(const float* low,const float* high,const float* camera_vp);
static const uint32_t PointFaces=63;
static const uint32_t MinimumClass=64,MaximumClass=1024;
inline unsigned FaceCount(uint32_t mask){unsigned n=0;for(;mask;mask&=mask-1)++n;return n;}
inline uint32_t AllFaces(const KichR4LocalLight& l){return l.type==2?1u:PointFaces;}
// Face axes, right/up and half-extent per unit depth exactly as LocalShadow;
// the extent includes the guard band and 5% so the test stays conservative.
inline bool FaceBasis(const KichR4LocalLight& l,unsigned face,Vec3& dir,Vec3& right,Vec3& up,float& extent){
    static const Vec3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    dir=l.type==2?kich_r4_scene::Normalize(Vec3{l.direction[0],l.direction[1],l.direction[2]}):axes[face%6];
    if(!kich_r4_lighting::Finite(dir.x) || kich_r4_scene::Dot(dir,dir)<.5f)return false;
    const Vec3 hint=fabsf(dir.y)>.99f?Vec3{0,0,1}:Vec3{0,1,0};
    right=kich_r4_scene::Normalize(kich_r4_scene::Cross(hint,dir));up=kich_r4_scene::Cross(dir,right);
    const float half=l.type==2?tanf((l.cone<3.1f?l.cone:3.1f)*.5f):1.f;
    extent=half*1.05f+.02f;return kich_r4_lighting::Finite(extent);
}
// Lit region of a face: |v.right|,|v.up| <= extent*(v.dir), v.dir <= range.
// True unless one of those five planes has the whole box outside.
inline bool FaceTouchesBox(const KichR4LocalLight& l,unsigned face,const float* low,const float* high){
    Vec3 dir,right,up;float e;if(!FaceBasis(l,face,dir,right,up,e))return true;
    const Vec3 planes[5]={{right.x-e*dir.x,right.y-e*dir.y,right.z-e*dir.z},{-right.x-e*dir.x,-right.y-e*dir.y,-right.z-e*dir.z},
        {up.x-e*dir.x,up.y-e*dir.y,up.z-e*dir.z},{-up.x-e*dir.x,-up.y-e*dir.y,-up.z-e*dir.z},dir};
    for(unsigned i=0;i<5;++i){const float n[3]={planes[i].x,planes[i].y,planes[i].z};float minimum=i==4?-l.range:0.f;
        for(unsigned a=0;a<3;++a)minimum+=n[a]*((n[a]>=0?low[a]:high[a])-l.position[a]);
        if(minimum>0)return false;}
    return true;
}
// Apex plus the four range-plane corners: they bound the face's lit region.
inline bool FacePoints(const KichR4LocalLight& l,unsigned face,float points[5][3]){
    Vec3 dir,right,up;float e;if(!FaceBasis(l,face,dir,right,up,e))return false;
    for(unsigned a=0;a<3;++a)points[0][a]=l.position[a];
    for(unsigned c=0;c<4;++c){const float sx=c&1?e:-e,sy=c&2?e:-e;
        const Vec3 v={dir.x+right.x*sx+up.x*sy,dir.y+right.y*sx+up.y*sy,dir.z+right.z*sx+up.z*sy};
        points[c+1][0]=l.position[0]+v.x*l.range;points[c+1][1]=l.position[1]+v.y*l.range;points[c+1][2]=l.position[2]+v.z*l.range;}
    return true;
}
// Homogeneous clip test of the five points, as kich_r4_visibility::Visible.
inline bool PointsVisible(const float points[5][3],const Matrix& vp){
    unsigned outside[6]={};
    for(unsigned i=0;i<5;++i){float c[4];
        for(unsigned a=0;a<4;++a){c[a]=points[i][0]*vp.m[a]+points[i][1]*vp.m[4+a]+points[i][2]*vp.m[8+a]+vp.m[12+a];if(!kich_r4_lighting::Finite(c[a]))return true;}
        const float pad=1.e-4f*(1.f+fabsf(c[3]));
        outside[0]+=c[0]<-c[3]-pad;outside[1]+=c[0]>c[3]+pad;outside[2]+=c[1]<-c[3]-pad;outside[3]+=c[1]>c[3]+pad;
        outside[4]+=c[2]<-pad;outside[5]+=c[2]>c[3]+pad;}
    for(unsigned p=0;p<6;++p)if(outside[p]==5)return false;
    return true;
}
// HUD receivers (weapon, hands) lie within this box around the camera and may
// sit outside the scene frustum (own FOV and near plane).
static const float HudReach=2.f;
// P0-S11 per-face decision. FaceTest yields the first five; the face
// admission history (KichR4FaceAdmission.h) adds HELD and BUDGET; NONE is a
// face the light does not have, or a light without a shadow this frame.
enum FaceReason {FaceNone=0,FaceVisible,FaceHud,FaceUnbounded,FaceFrustum,FaceOccluded,FaceHeld,FaceBudget,FaceReasonCount};
inline const char* FaceReasonName(unsigned r){
    static const char* names[FaceReasonCount]={"NONE","VISIBLE","HUD_BOX","UNBOUNDED","OUTSIDE_FRUSTUM","HOM_OCCLUDED","HOM_OCCLUDED_HELD","BUDGET_DROPPED"};
    return r<FaceReasonCount?names[r]:"INVALID";}
inline bool FaceAdmitted(unsigned r){return r==FaceVisible || r==FaceHud || r==FaceUnbounded || r==FaceHeld;}
// reasons (optional, 6 entries) receives each face's reason; unused faces NONE.
inline uint32_t FaceTest(const KichR4LocalLight& l,const float* camera,const float* camera_vp,BoxVisibility occlusion,uint8_t* reasons){
    const uint32_t all=AllFaces(l);const unsigned faces=l.type==2?1u:6u;
    if(reasons)for(unsigned face=0;face<6;++face)reasons[face]=face<faces?FaceUnbounded:FaceNone;
    if(!camera || !camera_vp)return all;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(camera_vp[a]))return all;
    Matrix vp;memcpy(vp.m,camera_vp,sizeof(vp.m));
    const float hud_low[3]={camera[0]-HudReach,camera[1]-HudReach,camera[2]-HudReach},hud_high[3]={camera[0]+HudReach,camera[1]+HudReach,camera[2]+HudReach};
    uint32_t mask=0;
    for(unsigned face=0;face<faces;++face){float points[5][3];uint8_t reason=FaceVisible;
        if(!FacePoints(l,face,points))reason=FaceUnbounded;
        else if(FaceTouchesBox(l,face,hud_low,hud_high))reason=FaceHud;
        else if(!PointsVisible(points,vp))reason=FaceFrustum;
        else if(occlusion){float low[3],high[3];
            for(unsigned a=0;a<3;++a){low[a]=high[a]=points[0][a];
                for(unsigned i=1;i<5;++i){low[a]=fminf(low[a],points[i][a]);high[a]=fmaxf(high[a],points[i][a]);}
                // The lit region also lies inside the light sphere.
                low[a]=fmaxf(low[a],l.position[a]-l.range);high[a]=fminf(high[a],l.position[a]+l.range);}
            bool visible=true;try{visible=occlusion(low,high,camera_vp);}catch(...){visible=true;}
            if(!visible)reason=FaceOccluded;}
        if(reasons)reasons[face]=reason;
        if(reason!=FaceFrustum && reason!=FaceOccluded)mask|=1u<<face;}
    return mask;
}
inline uint32_t FaceMask(const KichR4LocalLight& l,const float* camera,const float* camera_vp,BoxVisibility occlusion){
    return FaceTest(l,camera,camera_vp,occlusion,NULL);}
// R2 CLight_Compute_XFORM_and_VIS::compute_xf_spot before its own hysteresis:
// SMAP_adapt_optimal 768 scaled by sqrt(ssa), intensity^(1/16), duel^(1/4),
// (range/8)^(1/4) and sqrt(cone/90 deg), clamped to 32..1536. A point light
// uses its 90-degree faces; duel is per light (1 for a point light).
inline float R2Size(const KichR4LocalLight& l,const float* camera,const float* forward,float quality){
    float d2=0;for(unsigned a=0;a<3;++a){const float d=camera[a]-l.position[a];d2+=d*d;}
    float distance=sqrtf(d2)-l.range;if(!(distance>0))distance=0;
    float ssa=l.range*l.range/(1.f+distance*distance);ssa=ssa<0?0:ssa>1?1:ssa;
    const float i0=(l.color[0]+l.color[1]+l.color[2])/3.f,i1=l.color[0]*.2125f+l.color[1]*.7154f+l.color[2]*.0721f;
    const float intensity=(i0+i1)*.5f;
    float duel=1;
    if(l.type==2 && forward){const Vec3 d=kich_r4_scene::Normalize(Vec3{l.direction[0],l.direction[1],l.direction[2]});
        duel=1.f-.5f*(forward[0]*d.x+forward[1]*d.y+forward[2]*d.z);}
    const float wide=l.type==2?l.cone/1.57079632679f:1.f;
    const float factor=quality*sqrtf(ssa)*powf(intensity>0?intensity:0,1.f/16)*powf(duel>0?duel:0,.25f)*powf(l.range>0?l.range/8.f:0,.25f)*sqrtf(wide>0?wide:0);
    float size=floorf(factor*768.f);
    if(!kich_r4_lighting::Finite(size) || size<32)size=32;
    return size>1536?1536.f:size;
}
// Power-of-two class (geometric midpoints). previous keeps its class while the
// continuous size stays within 12% beyond that class's own midpoints.
inline uint32_t SizeClass(float size,uint32_t previous){
    uint32_t c=MinimumClass;while(c<MaximumClass && size>=float(c)*1.41421356f)c*=2;
    if(previous>=MinimumClass && previous<=MaximumClass && (previous&(previous-1))==0 && previous!=c &&
        size>=float(previous)/1.41421356f/1.12f && size<float(previous)*1.41421356f*1.12f)return previous;
    return c;
}
// Shared byte budget: halve the largest map (the least important on ties,
// lights are sorted by importance) down to MinimumClass, then drop the least
// important shadows. Below 64 texels the +-3 texel filter spans metres and a
// grazing receiver leaks light past a close blocker.
inline uint64_t Planned(const std::vector<uint32_t>& sizes,const std::vector<uint32_t>& masks){uint64_t total=0;
    for(size_t i=0;i<sizes.size() && i<masks.size();++i)if(masks[i] && sizes[i])total+=kich_r4_shadow_pool::Payload(FaceCount(masks[i]),sizes[i]);
    return total;}
inline void FitBudget(std::vector<uint32_t>& sizes,std::vector<uint32_t>& masks,uint64_t budget=kich_r4_shadow_pool::BudgetBytes){
    for(;;){size_t largest=sizes.size();
        for(size_t i=0;i<sizes.size() && i<masks.size();++i)if(masks[i] && sizes[i] && (largest==sizes.size() || sizes[i]>=sizes[largest]))largest=i;
        if(Planned(sizes,masks)<=budget || largest==sizes.size() || sizes[largest]<=MinimumClass)break;
        sizes[largest]/=2;}
    for(size_t i=masks.size();i-- && Planned(sizes,masks)>budget;)masks[i]=0;
}
// Camera forward from a row-vector D3D view-projection (its w column).
inline bool Forward(const float* vp,float* out){
    const float x=vp[3],y=vp[7],z=vp[11],n=sqrtf(x*x+y*y+z*z);
    if(!kich_r4_lighting::Finite(n) || n<1.e-12f)return false;
    out[0]=x/n;out[1]=y/n;out[2]=z/n;return true;
}
}
#endif
