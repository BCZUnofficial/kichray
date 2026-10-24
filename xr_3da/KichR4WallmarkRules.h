#ifndef KICH_R4_WALLMARK_RULES_H
#define KICH_R4_WALLMARK_RULES_H
#include "KichR4CaptureData.h"
#include <string.h>
namespace kich_r4_wallmark {
// SoC CBlender_Screen_SET stages and X-Ray 1.6 stub_default_ma/simple.
// Read the color contract, rather than guessing from the blend factors.
enum ColorMode {Modulate,Modulate2x,Modulate4x,TextureOnly,FadeToVertex};
inline uint32_t Color(const char* shader,uint32_t stage0,uint32_t stage1){
    if(shader && !strcmp(shader,"stub_default_ma"))return FadeToVertex;
    if(shader && !strcmp(shader,"simple"))return TextureOnly;
    if(stage0==2 && stage1==12)return FadeToVertex; // SELECTARG1 + BLENDDIFFUSEALPHA
    if(stage0==2)return TextureOnly;
    if(stage0==5)return Modulate2x;
    if(stage0==6)return Modulate4x;
    return Modulate;
}
inline void Capture(uint32_t scope,bool screen,uint32_t color,kich_r4_capture::Command& command){
    if(!scope || screen)return;
    command.effect=6;command.wallmark_color=color;
    command.semantic=scope==2?kich_r4_capture::CAPTURE_SKELETAL_WALLMARK:kich_r4_capture::CAPTURE_STATIC_WALLMARK;
}
}
#endif
