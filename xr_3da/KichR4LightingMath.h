#ifndef KICH_R4_LIGHTING_MATH_H
#define KICH_R4_LIGHTING_MATH_H
#include "KichR4TestScene.h"
#include "KichR4Lighting.h"
#include <stddef.h>
#include <float.h>
namespace kich_r4_lighting {
using namespace kich_r4_scene;
inline bool Finite(float v){return v==v && v>=-1.e20f && v<=1.e20f;}
inline bool Inverse(const Matrix& a,Matrix& out){
    double row[4][8]={};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        if(!Finite(a.m[i*4+j]))return false;
        row[i][j]=a.m[i*4+j];row[i][j+4]=i==j?1:0;
    }
    for(unsigned c=0;c<4;++c){unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r)if(fabs(row[r][c])>fabs(row[pivot][c]))pivot=r;
        if(fabs(row[pivot][c])<1.e-12)return false;
        for(unsigned j=0;j<8;++j){const double t=row[pivot][j];row[pivot][j]=row[c][j];row[c][j]=t;}
        const double divisor=row[c][c];for(unsigned j=0;j<8;++j)row[c][j]/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){const double factor=row[r][c];for(unsigned j=0;j<8;++j)row[r][j]-=factor*row[c][j];}
    }
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){out.m[i*4+j]=static_cast<float>(row[i][j+4]);if(!Finite(out.m[i*4+j]))return false;}
    return true;
}
inline Matrix NormalMatrix(const Matrix& world){Matrix inv;
    if(!Inverse(world,inv))return Identity();
    Matrix n={};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)n.m[i*4+j]=inv.m[j*4+i];n.m[15]=1;return n;
}
inline Vec3 Direction(Vec3 n,const Matrix& m){const Vec3 out={n.x*m.m[0]+n.y*m.m[4]+n.z*m.m[8],
    n.x*m.m[1]+n.y*m.m[5]+n.z*m.m[9],n.x*m.m[2]+n.y*m.m[6]+n.z*m.m[10]};return out;}
inline Matrix StableView(Vec3 eye,Vec3 forward){forward=Normalize(forward);
    const Vec3 hint=fabsf(forward.y)>.99f?Vec3{0,0,1}:Vec3{0,1,0};
    const Vec3 right=Normalize(Cross(hint,forward)),up=Cross(forward,right);
    const Matrix view={{right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,
        -Dot(right,eye),-Dot(up,eye),-Dot(forward,eye),1}};return view;
}
inline Matrix Projection(float fov,float near_z,float far_z){Matrix p={};const float y=1.f/tanf(fov*.5f);
    p.m[0]=p.m[5]=y;p.m[10]=far_z/(far_z-near_z);p.m[11]=1;p.m[14]=-near_z*p.m[10];return p;}
inline Matrix SunShadow(const float* camera,const float* direction,float span=512.f,unsigned resolution=2048){
    const Vec3 dir=Normalize(Vec3{direction[0],direction[1],direction[2]});
    const Vec3 center={camera[0],camera[1],camera[2]};
    Matrix view=StableView(Vec3{0,0,0},dir);const Vec3 light_center=Direction(center,view);
    if(!Finite(span) || span<=0)span=512.f;
    if(!resolution)resolution=2048;
    const float texel=span/static_cast<float>(resolution),depth_step=512.f/2048.f;
    // Quantize the projection center in light space. Translating the camera
    // within a texel leaves world-to-shadow coordinates exactly unchanged.
    view.m[12]=-floorf(light_center.x/texel+.5f)*texel;
    view.m[13]=-floorf(light_center.y/texel+.5f)*texel;
    // Tighten only receiver XY coverage. Both cascades need the full upstream
    // caster interval; otherwise a distant caster cannot shadow a near receiver.
    // Keep identical depth quantization so choosing a cascade never changes Z.
    view.m[14]=512.f-floorf(light_center.z/depth_step+.5f)*depth_step;
    Matrix p=Identity();p.m[0]=p.m[5]=2.f/span;p.m[10]=1.f/1024.f;
    return Multiply(view,p);
}
inline float LocalShadowNear(float range){return range<.4f?range*.25f:.1f;}
inline Matrix LocalShadow(const KichR4LocalLight& l,unsigned face){
    static const Vec3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const Vec3 eye={l.position[0],l.position[1],l.position[2]};
    const Vec3 dir=l.type==2?Vec3{l.direction[0],l.direction[1],l.direction[2]}:axes[face%6];
    return Multiply(StableView(eye,dir),Projection(l.type==2?l.cone:1.57079632679f,LocalShadowNear(l.range),l.range));
}
struct Constants {
    Matrix world,mvp,normal_matrix,inverse_vp,shadow[6];
    float camera[4],screen[4],sun_direction[4],sun_color[4],ambient[4],hemi[4],fog[4];
    float light_position[4],light_direction[4],light_color[4],alpha[4];
    float material[4],color_scale[4],color_bias[4],sun_scale_bias[4],tree_wave[4],tree_wind[4];
};
static_assert(sizeof(Constants)==912,"B4.4 HLSL cbuffer size");
static_assert(offsetof(Constants,normal_matrix)==128 && offsetof(Constants,inverse_vp)==192 &&
    offsetof(Constants,shadow)==256 && offsetof(Constants,camera)==640 && offsetof(Constants,alpha)==800,"B4.4 HLSL offsets");
inline bool ValidLight(const KichR4LocalLight& l){
    if((l.type!=1 && l.type!=2) || !Finite(l.range) || l.range<=.05f || l.range>10000)return false;
    for(unsigned i=0;i<3;++i)if(!Finite(l.position[i]) || !Finite(l.direction[i]) || !Finite(l.color[i]) || l.color[i]<0)return false;
    if(l.type==2){const float length=l.direction[0]*l.direction[0]+l.direction[1]*l.direction[1]+l.direction[2]*l.direction[2];
        if(!Finite(l.cone) || l.cone<=.01f || l.cone>2.12f || length<.01f || length>100)return false;}
    return true;
}
}
#endif
