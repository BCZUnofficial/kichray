#ifndef KICH_R4_LIGHTING_MATH_H
#define KICH_R4_LIGHTING_MATH_H
#include "KichR4TestScene.h"
#include "KichR4Lighting.h"
#include <stddef.h>
#include <float.h>
namespace kich_r4_lighting {
using namespace kich_r4_scene;
inline bool Finite(float v){return v==v && v>=-1.e20f && v<=1.e20f;}
inline bool Inverse(const Matrix& a,Matrix& out,const float* relative_origin=NULL){
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
    // Remove the camera translation before rounding the inverse to float.
    // Inverting a separately recomposed view/projection would not match the
    // exact full matrix used by the rasterizer. This keeps that matrix while
    // avoiding large absolute coordinates divided by a small homogeneous w.
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        const double value=row[i][j+4]-(relative_origin && j<3?double(relative_origin[j])*row[i][7]:0);
        out.m[i*4+j]=static_cast<float>(value);if(!Finite(out.m[i*4+j]))return false;
    }
    return true;
}
inline bool ReceiverInverse(const Matrix& raster_vp,Matrix& out,const float* relative_origin){
    // The receiver target stores interpolated clip W. Invert the affine map
    // world -> (clip X, clip Y, clip W), retaining the exact raster matrix.
    // This never re-encodes linear depth into a near-plane-dependent D24 value.
    Matrix affine=raster_vp;
    for(unsigned row=0;row<4;++row){affine.m[row*4+2]=raster_vp.m[row*4+3];affine.m[row*4+3]=row==3?1.f:0.f;}
    if(Inverse(affine,out,relative_origin))return true;
    // Orthographic/test projections have constant W. The zero validity slot
    // selects the existing inverse/depth path, rather than stale coefficients.
    memset(&out,0,sizeof(out));return false;
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
// basis (optional, P0-S4): held light orientation. The texel grid, snapping and
// depth axis stay fixed to it while an oblique shear about the snapped centre
// depth projects along the true, continuously moving direction. Receivers near
// the centre depth keep their texels; shadows still move smoothly with the sun.
inline Matrix SunShadow(const float* camera,const float* direction,float span=512.f,unsigned resolution=2048,const float* basis=NULL){
    const Vec3 sun=Normalize(Vec3{direction[0],direction[1],direction[2]});
    const Vec3 held=basis?Normalize(Vec3{basis[0],basis[1],basis[2]}):sun;
    const Vec3 dir=Dot(held,sun)>.5f?held:sun;
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
    // Oblique shear: x' = x - k.x*(z-512), y' = y - k.y*(z-512), k = true sun
    // direction in the held basis over its depth component. Zero when equal.
    const Vec3 local=Direction(sun,view);
    if(basis && local.z>.5f){Matrix shear=Identity();const float kx=local.x/local.z,ky=local.y/local.z;
        shear.m[8]=-kx;shear.m[9]=-ky;shear.m[12]=512.f*kx;shear.m[13]=512.f*ky;view=Multiply(view,shear);}
    return Multiply(view,p);
}
// P0-S3 shadow-stable sun. Every cascade basis rotates about the world origin,
// so a continuously moving sun slides each shadow texel by |p|*dtheta across
// the world even with texel snapping (~4 contact texels/s at 200 m, SoC time
// factor 10). Hold the direction until the sun has turned more than the step
// (cos_step), then jump once. Returns true when the held direction changed.
inline bool HoldSunDirection(float* held,const float* current,float cos_step){
    if(!Finite(current[0]) || !Finite(current[1]) || !Finite(current[2]))return false;
    const Vec3 now=Normalize(Vec3{current[0],current[1],current[2]});
    if(Dot(now,now)<.5f)return false;
    const Vec3 kept={held[0],held[1],held[2]};
    if(Finite(kept.x) && Finite(kept.y) && Finite(kept.z) && fabsf(Dot(kept,kept)-1)<1e-3f && Dot(kept,now)>=cos_step)return false;
    held[0]=now.x;held[1]=now.y;held[2]=now.z;return true;
}
// P0-S5: 1024, 2048 or 4096; anything else is the E1.9 2048.
inline uint32_t SunMapSize(uint32_t requested){return requested==1024 || requested==4096?requested:2048u;}
inline uint32_t SunMapSize(const KichR4LightingFrame& lighting){return SunMapSize(lighting.sun_map_size);}
// Every sun cascade of a frame: its held basis when valid, else the sun itself.
inline Matrix SunCascade(const KichR4LightingFrame& lighting,const float* camera,float span){
    return SunShadow(camera,lighting.sun_direction,span,SunMapSize(lighting),lighting.sun_basis_valid?lighting.sun_basis:NULL);
}
// P0-S2 contact cascade: same texel-snapped, fixed-depth construction as the
// 128/512 m cascades, centred on the frame's stable r2_sun_near sphere.
inline bool SunContactShadow(const KichR4LightingFrame& lighting,Matrix& out){
    if(!lighting.sun_contact_valid || !Finite(lighting.sun_contact[3]) || lighting.sun_contact[3]<=0)return false;
    out=SunCascade(lighting,lighting.sun_contact,lighting.sun_contact[3]);return true;
}
inline float LocalShadowNear(float range){return range<.4f?range*.25f:.1f;}
inline Matrix LocalShadow(const KichR4LocalLight& l,unsigned face,unsigned resolution=512){
    static const Vec3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const Vec3 eye={l.position[0],l.position[1],l.position[2]};
    const Vec3 dir=l.type==2?Vec3{l.direction[0],l.direction[1],l.direction[2]}:axes[face%6];
    Matrix projection=Projection(l.type==2?l.cone:1.57079632679f,LocalShadowNear(l.range),l.range);
    if(l.type!=2){
        // Two rendered texels around each 90-degree face keep the complete
        // three-tap footprint inside real geometry at cube-face transitions.
        // Camera/caster collection and sampling must use the same resolution.
        const unsigned size=resolution<16?16:resolution;
        projection.m[0]=projection.m[5]=float(size-4)/float(size);
    }
    return Multiply(StableView(eye,dir),projection);
}
struct Constants {
    Matrix world,mvp,normal_matrix,inverse_vp,shadow[6];
    float camera[4],screen[4],sun_direction[4],sun_color[4],ambient[4],hemi[4],fog[4];
    float light_position[4],light_direction[4],light_color[4],alpha[4];
    float material[4],color_scale[4],color_bias[4],sun_scale_bias[4],tree_wave[4],tree_wind[4];
    float fog_params[4],fog_plane[4];
    Matrix receiver_inverse;
    // P0-L1: R2 L_ambient (.w r2_gloss_factor) and env_color (.w cubemap lerp).
    float env_ambient[4],env_color[4];
    // P0-S1: R2 m_sunmask rows for world positions; zero when no cloud mask.
    float sunmask_u[4],sunmask_v[4];
};
static_assert(sizeof(Constants)==1072,"P0-S1 HLSL cbuffer size");
static_assert(offsetof(Constants,normal_matrix)==128 && offsetof(Constants,inverse_vp)==192 &&
    offsetof(Constants,shadow)==256 && offsetof(Constants,camera)==640 && offsetof(Constants,alpha)==800 &&
    offsetof(Constants,fog_params)==912 && offsetof(Constants,fog_plane)==928 &&
    offsetof(Constants,receiver_inverse)==944 && offsetof(Constants,env_ambient)==1008 &&
    offsetof(Constants,env_color)==1024 && offsetof(Constants,sunmask_u)==1040 &&
    offsetof(Constants,sunmask_v)==1056,"P0-S1 HLSL offsets");
inline void FogConstants(const KichR4LightingFrame& input,const Matrix& vp,Constants& out){
    memset(out.fog_params,0,sizeof(out.fog_params));memset(out.fog_plane,0,sizeof(out.fog_plane));
    if(!input.fog_valid || !Finite(input.fog_near) || !Finite(input.fog_far) || input.fog_far<=input.fog_near)return;
    const double reciprocal=1.0/(double(input.fog_far)-input.fog_near);
    const double offset=-double(input.fog_near)*reciprocal;
    if(reciprocal>1.e10 || !Finite(float(offset)))return;
    out.fog_params[0]=float(offset);out.fog_params[1]=float(reciprocal);
    // R2 cl_fog_plane: normalized view plane, distinct from radial combine fog.
    double plane[4];for(unsigned i=0;i<4;++i)plane[i]=-double(vp.m[i*4+3])-vp.m[i*4+2];
    const double length=sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
    if(!(length>1.e-12))return;
    for(unsigned i=0;i<4;++i)plane[i]/=-length;
    for(unsigned i=0;i<3;++i)out.fog_plane[i]=float(-plane[i]*reciprocal);
    out.fog_plane[3]=float(1-(plane[3]-input.fog_near)*reciprocal);
    for(unsigned i=0;i<4;++i)if(!Finite(out.fog_plane[i])){memset(out.fog_plane,0,sizeof(out.fog_plane));return;}
    out.fog_params[3]=1; // forward plane is valid; .z is a per-draw opt-in.
}
inline bool ValidLight(const KichR4LocalLight& l){
    if((l.type!=1 && l.type!=2) || !Finite(l.range) || l.range<=.05f || l.range>10000)return false;
    for(unsigned i=0;i<3;++i)if(!Finite(l.position[i]) || !Finite(l.direction[i]) || !Finite(l.color[i]) || l.color[i]<0)return false;
    if(l.type==2){const float length=l.direction[0]*l.direction[0]+l.direction[1]*l.direction[1]+l.direction[2]*l.direction[2];
        if(!Finite(l.cone) || l.cone<=.01f || l.cone>2.12f || length<.01f || length>100)return false;}
    return true;
}
}
#endif
