#ifndef KICH_R4_POSTPROCESS_NOISE_H
#define KICH_R4_POSTPROCESS_NOISE_H
#include <stdint.h>
#include <cmath>
namespace kich_r4_noise {
inline const char* Texture(){return "fx\\fx_noise2";}
inline float Amount(float requested){
    if(!(requested>0))return 0;
    if(requested>=1)return 1;
    // R2 packs (1-noise) into the vertex color's eight-bit alpha.
    return 1.f-std::floor((1.f-requested)*255.f)/255.f;
}
// R2 updates the two random offsets once when the noise timer expires, even
// across a long frame. Keep the remainder without a potentially unbounded loop.
struct Cadence {
    double remaining=0;uint32_t frame=0,phase=0;bool started=false;
    float Advance(uint32_t next_frame,float delta,float fps){
        if(started && frame==next_frame)return static_cast<float>(phase);
        if(started && next_frame<frame)remaining=0;
        frame=next_frame;started=true;
        if(!(fps>0) || !std::isfinite(fps)){remaining=0;return static_cast<float>(phase);}
        if(!(delta>0) || !std::isfinite(delta))return static_cast<float>(phase);
        remaining-=delta;
        if(remaining<0){phase=(phase+1)&0xffffffu;const double period=1.0/fps;
            remaining=std::fmod(remaining,period);if(remaining<0)remaining+=period;}
        return static_cast<float>(phase);
    }
};
inline uint32_t Mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
inline bool Coordinates(uint32_t width,uint32_t height,uint32_t texture_width,uint32_t texture_height,
                        float scale,float phase,float (&uv)[4]){
    if(!texture_width || !texture_height || !(scale>0) || !std::isfinite(scale))return false;
    // Match u_calc_tc_noise in R2 and the 1.6 R4 donor, including integer tile
    // counts and the extra tile beyond the viewport. U and V use separate seeds.
    const float w=std::ceil(float(texture_width)*scale+1e-7f),h=std::ceil(float(texture_height)*scale+1e-7f);
    if(!(w>=1 && h>=1) || double(w)>UINT32_MAX || double(h)>UINT32_MAX)return false;
    const uint32_t tw=static_cast<uint32_t>(w),th=static_cast<uint32_t>(h);
    const uint32_t tick=phase>=0 && phase<=16777215.f?static_cast<uint32_t>(phase):0;
    uv[0]=(float(Mix(tick^0xa511e9b3u)%tw)+.5f)/w;
    uv[1]=(float(Mix(tick^0x63d83595u)%th)+.5f)/h;
    uv[2]=float(width/tw)+1;uv[3]=float(height/th)+1;return true;
}
}
#endif
