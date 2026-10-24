#ifndef KICH_R4_SURFACE_STATE_H
#define KICH_R4_SURFACE_STATE_H
#include <stdint.h>
// Numeric comparisons/operations follow the D3D9 source contract; no SDK types.
#pragma pack(push,4)
typedef struct KichR4SamplerState {
    uint32_t u,v,w,min_filter,mag_filter,mip_filter,anisotropy,border,lod_bias_bits,max_mip;
} KichR4SamplerState;
typedef struct KichR4SurfaceState {
    uint32_t valid,depth_func,blend_op,color_mask;
    uint32_t separate_alpha,source_alpha,dest_alpha,alpha_op;
    uint32_t stencil,stencil_func,stencil_ref,stencil_read,stencil_write;
    uint32_t stencil_fail,stencil_zfail,stencil_pass;
    KichR4SamplerState samplers[8]; // u==0: native default (wrap/clamp policy).
} KichR4SurfaceState;
#pragma pack(pop)
#ifdef __cplusplus
#include <string.h>
inline KichR4SurfaceState KichR4DefaultSurface(){
    const KichR4SurfaceState s={1,4,1,15,0,2,1,1,0,8,0,255,255,1,1,1,{}};return s;
}
inline bool KichR4ValidSurface(const KichR4SurfaceState& s){
    for(unsigned i=0;i<8;++i){const KichR4SamplerState& t=s.samplers[i];float bias;memcpy(&bias,&t.lod_bias_bits,4);if(t.u &&
        (t.u>5 || !t.v || t.v>5 || !t.w || t.w>5 || !t.min_filter || t.min_filter>3 ||
        !t.mag_filter || t.mag_filter>3 || t.mip_filter>2 || !t.anisotropy || t.anisotropy>16 || !(bias>=-16 && bias<=15.99f)))return false;}
    return s.valid<=1 && (!s.valid || (s.depth_func>=1 && s.depth_func<=8 && s.blend_op>=1 && s.blend_op<=5 &&
        s.color_mask<=15 && s.separate_alpha<=1 && s.source_alpha>=1 && s.source_alpha<=10 && s.dest_alpha>=1 && s.dest_alpha<=10 &&
        s.alpha_op>=1 && s.alpha_op<=5 && s.stencil<=1 && s.stencil_func>=1 && s.stencil_func<=8 &&
        s.stencil_ref<=255 && s.stencil_read<=255 && s.stencil_write<=255 && s.stencil_fail>=1 && s.stencil_fail<=8 &&
        s.stencil_zfail>=1 && s.stencil_zfail<=8 && s.stencil_pass>=1 && s.stencil_pass<=8));
}
template<class Get> inline KichR4SamplerState KichR4ReadSampler(Get get){
    const KichR4SamplerState s={get(1,1),get(2,1),get(3,1),get(6,2),get(5,2),get(7,2),get(10,1),get(4,0),get(8,0),get(9,0)};return s;
}
template<class Get> inline KichR4SurfaceState KichR4ReadSurface(Get get){
    KichR4SurfaceState s=KichR4DefaultSurface();
    s.depth_func=get(23,4);s.blend_op=get(171,1);s.color_mask=get(168,15);
    s.separate_alpha=get(206,0);s.source_alpha=get(207,2);s.dest_alpha=get(208,1);s.alpha_op=get(209,1);
    s.stencil=get(52,0);s.stencil_func=get(56,8);s.stencil_ref=get(57,0)&255;s.stencil_read=get(58,255)&255;s.stencil_write=get(59,255)&255;
    s.stencil_fail=get(53,1);s.stencil_zfail=get(54,1);s.stencil_pass=get(55,1);return s;
}
inline void KichR4ReceiverState(KichR4SurfaceState& s,bool write){
    if(!s.valid)s=KichR4DefaultSurface();
    s.stencil=1;s.stencil_ref=1;s.stencil_read=1;s.stencil_write=write?1:0;
    s.stencil_func=write?8:3;s.stencil_fail=s.stencil_zfail=1;s.stencil_pass=write?3:1;
}
#endif
#endif
