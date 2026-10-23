#ifndef KICH_R4_OPTICAL_RULES_H
#define KICH_R4_OPTICAL_RULES_H
#include <math.h>
#include "KichR4CaptureInput.h"
namespace kich_r4_optics {
enum { CelestialSource = 9 };
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
inline void SkyDepth(float* clip){clip[2]=clip[3];}
inline void SkyStyle(kich_r4_capture::Style& style){
    style.depth=1;style.write=0;style.min_z=0;style.max_z=1;
}
}
#endif
