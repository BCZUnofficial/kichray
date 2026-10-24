// P0-S3: held sun direction. Small per-frame sun motion must leave every sun
// cascade matrix bit-identical; a turn past the step must update it once.
#include "KichR4LightingMath.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace kich_r4_lighting;
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static const float kCosStep=.99999048f; // cos(0.25 deg), as KichR4GameEngine.cpp
static void Direction(float degrees,float* out){const float a=degrees*3.14159265f/180;
    out[0]=-.8975944519f*cosf(a);out[1]=-.3139902651f;out[2]=-.8975944519f*sinf(a);}
int main(){try{
    float held[3]={0,0,0},sun[3];Direction(0,sun);
    Require(HoldSunDirection(held,sun,kCosStep),"first valid sun initialises the held direction");
    const float camera[3]={-125.4335327f,-7.744039536f,-148.8468323f};
    const Matrix first=SunShadow(camera,held,128);
    // SoC time factor 10: ~0.042 deg per real second; 3 s of 60 fps frames.
    unsigned changes=0;
    for(unsigned frame=1;frame<=180;++frame){Direction(.042f*frame/60,sun);
        if(HoldSunDirection(held,sun,kCosStep))++changes;}
    Require(changes==0,"sub-step sun motion keeps the held direction");
    const Matrix still=SunShadow(camera,held,128);
    Require(memcmp(first.m,still.m,sizeof(first.m))==0,"held direction keeps the cascade matrix bit-identical");
    Direction(.3f,sun);Require(HoldSunDirection(held,sun,kCosStep),"turn past 0.25 deg updates the held direction");
    const Vec3 a=Normalize(Vec3{sun[0],sun[1],sun[2]});
    Require(fabsf(held[0]-a.x)<1e-6f && fabsf(held[1]-a.y)<1e-6f && fabsf(held[2]-a.z)<1e-6f,"update jumps to the current sun");
    Require(!HoldSunDirection(held,sun,kCosStep),"same direction is idempotent (two calls per frame)");
    const float bad[3]={NAN,0,0},zero[3]={0,0,0};float kept[3]={held[0],held[1],held[2]};
    Require(!HoldSunDirection(held,bad,kCosStep) && !HoldSunDirection(held,zero,kCosStep) && memcmp(kept,held,sizeof(kept))==0,
        "non-finite or zero sun keeps the last valid direction");
    // A weather/time jump (sleep) updates immediately.
    Direction(40,sun);Require(HoldSunDirection(held,sun,kCosStep),"large sun change applies at once");
    std::printf("P0-S3 held sun direction: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
