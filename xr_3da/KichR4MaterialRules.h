#ifndef KICH_R4_MATERIAL_RULES_H
#define KICH_R4_MATERIAL_RULES_H
#include <stdint.h>
#include <string.h>

namespace kich_r4_material {
// These source E[4] shaders write constant irradiance, with no texture sampler.
// Fold the known contract into the normal base pass; never draw a second base
// texture or a white fallback for the samplerless accumulator pass.
inline float EmissiveGain(const char* pixel_shader){
    if(pixel_shader && !strcmp(pixel_shader,"accum_emissive"))return 8;
    if(pixel_shader && !strcmp(pixel_shader,"accum_emissivel"))return .5f;
    return 0;
}
inline float EmissiveSpecular(const char* pixel_shader){
    return pixel_shader && !strcmp(pixel_shader,"accum_emissive")?8.f:0.f;
}
/* Adapted selection contract from X-Ray 1.6 r__dsgraph_build/render:
 * E[4].bDistort is an independent special queue; a missing normal element
 * means no ordinary draw, not reuse of the previously bound material. */
struct Element {
    uint32_t passes;bool distort,emissive;
    Element(uint32_t n=0,bool d=false,bool e=false):passes(n),distort(d),emissive(e){}
};
struct Selection {uint32_t normal,distortion;
    Selection():normal(UINT32_MAX),distortion(UINT32_MAX){}};
inline Selection Select(const Element (&elements)[6],bool distortion_enabled=true){
    Selection result;
    if(elements[0].passes && !elements[0].distort)result.normal=0;
    else if(elements[1].passes && !elements[1].distort)result.normal=1;
    if(distortion_enabled){
        if(elements[4].passes && elements[4].distort)result.distortion=4;
        else if(elements[0].passes && elements[0].distort)result.distortion=0;
        else if(elements[1].passes && elements[1].distort)result.distortion=1;
    }
    return result;
}
inline bool Contains(const char* value,const char* part){return value && strstr(value,part)!=0;}
inline uint32_t Effect(const char* pixel_shader,bool distortion,bool wallmark){
    if(distortion)return 5;
    if(wallmark)return 6;
    // Stock R1 simple.ps returns only tex2D(s_base), without scene lighting.
    if(pixel_shader && !strcmp(pixel_shader,"simple"))return 1;
    if(Contains(pixel_shader,"sky"))return 2;
    if(Contains(pixel_shader,"cloud"))return 3;
    if(Contains(pixel_shader,"water"))return 4;
    if(Contains(pixel_shader,"distort"))return 5;
    if(Contains(pixel_shader,"wmark"))return 6;
    if(Contains(pixel_shader,"particle") || Contains(pixel_shader,"flare") || Contains(pixel_shader,"font") ||
        Contains(pixel_shader,"hud") || Contains(pixel_shader,"rain"))return 1;
    return 0;
}
inline bool IsDistortion(uint32_t effect){return effect==5;}
}
#endif
