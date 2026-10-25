#ifndef KICH_R4_OPTICAL_RULES_H
#define KICH_R4_OPTICAL_RULES_H
#include <math.h>
#include "KichR4CaptureInput.h"
namespace kich_r4_optics {
enum { CelestialSource = 9 };
enum { DebugViewCount = 13 };
struct GlowVisibility {
    float value,camera[3];uint32_t time;bool known;
    GlowVisibility():value(0),time(0),known(false){camera[0]=camera[1]=camera[2]=0;}
    void Reset(){value=0;known=false;}
    float Step(bool visible,uint32_t now,const float* eye){
        float distance2=0;for(unsigned i=0;i<3;++i){const float d=eye[i]-camera[i];distance2+=d*d;}
        uint32_t elapsed=known?now-time:16;
        if(!known || elapsed>500 || distance2>16){value=0;elapsed=16;}
        if(elapsed>100)elapsed=100;
        const float step=float(elapsed)/(visible?150.f:100.f);
        value=visible?(value+step<1?value+step:1):(value>step?value-step:0);
        time=now;known=true;for(unsigned i=0;i<3;++i)camera[i]=eye[i];return value;
    }
};
// A centre collision ray cannot describe a partially visible billboard and
// omits moving renderables. The completed opaque depth buffer is authoritative.
inline void GlowStyle(kich_r4_capture::Style& style){
    style.depth=1;style.write=0;style.cull=KICH_R4_CULL_NONE;style.min_z=0;style.max_z=1;
    style.surface.depth_func=4; // D3DCMP_LESSEQUAL, no radius-sized depth bias.
}
inline const char* DebugView(unsigned view){
    const char* names[]={"NORMAL","ALBEDO","MAPPED_NORMAL","DISTANCE","TOTAL_LIGHT","HEMI",
        "BAKED_RGB_RAW","BAKED_SUN_MASK_RAW","SUN_PCF_VISIBILITY","SOURCE_NORMAL",
        "CELESTIAL_ONLY_MASKED","CELESTIAL_ONLY_UNMASKED","CELESTIAL_TRIANGLES"};
    return view<DebugViewCount?names[view]:"INVALID";
}
// P0-S11 debug pass mask. BAKED_RGB_RAW (6) and SOURCE_NORMAL (9) write their
// diagnostic into the albedo target; passes that blend ordinary albedo over it
// afterwards (static and dynamic wallmarks) would contaminate the channel
// shown (a bar floor's wm_graz3 read as a "shadow" in SOURCE_NORMAL). ALBEDO
// (1) and every lit view keep the overlays: they are real albedo there.
inline bool AlbedoOverlays(unsigned view){return view!=6 && view!=9;}
// X-Ray glow convention: zero direction is omnidirectional; otherwise the
// emitter faces the camera only for a positive direction-to-observer cosine.
inline float GlowFacing(const float* direction,const float* to_camera){
    const float d2=direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
    if(d2==0)return 1;
    const float r2=to_camera[0]*to_camera[0]+to_camera[1]*to_camera[1]+to_camera[2]*to_camera[2];
    if(!(d2>0) || !(r2>1.e-12f))return 0;
    const float scale=(direction[0]*to_camera[0]+direction[1]*to_camera[1]+direction[2]*to_camera[2])/sqrtf(d2*r2);
    return scale>0?(scale<1?scale:1):0;
}
// The mask is sampled from completed GBuffer depth in PSCelestial. Keep the
// quad inside the clip volume without comparing interpolated far-plane depth.
inline void SkyDepth(float* clip){clip[2]=clip[3]*.5f;}
inline void SkyStyle(kich_r4_capture::Style& style){
    style.depth=0;style.write=0;style.min_z=0;style.max_z=1;
}
}
#endif
