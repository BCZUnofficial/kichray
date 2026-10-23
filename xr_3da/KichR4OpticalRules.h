#ifndef KICH_R4_OPTICAL_RULES_H
#define KICH_R4_OPTICAL_RULES_H
#include <math.h>
#include "KichR4CaptureInput.h"
namespace kich_r4_optics {
enum { CelestialSource = 9 };
enum { DebugViewCount = 13 };
inline const char* DebugView(unsigned view){
    const char* names[]={"NORMAL","ALBEDO","MAPPED_NORMAL","DISTANCE","TOTAL_LIGHT","HEMI",
        "BAKED_RGB_RAW","BAKED_SUN_MASK_RAW","SUN_PCF_VISIBILITY","SOURCE_NORMAL",
        "CELESTIAL_ONLY_MASKED","CELESTIAL_ONLY_UNMASKED","CELESTIAL_TRIANGLES"};
    return view<DebugViewCount?names[view]:"INVALID";
}
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
