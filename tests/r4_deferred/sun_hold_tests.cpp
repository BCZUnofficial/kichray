// P0-S3: held sun direction. Small per-frame sun motion must leave every sun
// cascade matrix bit-identical; a turn past the step must update it once.
#include "KichR4LightingMath.h"
#include "KichR4DetailData.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace kich_r4_lighting;
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static const float kCosStep=.99999048f; // cos(0.25 deg) step for the hold function itself
struct float4v{float x,y,z,w;};
static float4v Project(const Matrix& m,float x,float y,float z){
    float4v r={x*m.m[0]+y*m.m[4]+z*m.m[8]+m.m[12],x*m.m[1]+y*m.m[5]+z*m.m[9]+m.m[13],
        x*m.m[2]+y*m.m[6]+z*m.m[10]+m.m[14],x*m.m[3]+y*m.m[7]+z*m.m[11]+m.m[15]};
    r.x/=r.w;r.y/=r.w;r.z/=r.w;return r;}
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
    // P0-S4 held basis + oblique shear (smooth sun, fixed texel grid).
    float basis[3];Direction(0,basis);
    for(float degrees:{0.f,.5f,1.5f,2.9f}){Direction(degrees,sun);const Matrix m=SunShadow(camera,sun,128,2048,basis);
        // Points along the true ray share one texel coordinate: shadows follow the sun.
        const float p[3]={-120.f,-8.f,-150.f};
        const float4v a=Project(m,p[0],p[1],p[2]),b=Project(m,p[0]+sun[0]*7,p[1]+sun[1]*7,p[2]+sun[2]*7);
        Require(fabsf(a.x-b.x)<1e-5f && fabsf(a.y-b.y)<1e-5f,"sheared cascade projects along the true sun direction");
        Require(b.z>a.z,"depth still increases away from the sun");}
    // Receivers on the held centre plane keep their texel while the sun moves.
    {Direction(.2f,sun);const Matrix m0=SunShadow(camera,sun,128,2048,basis);Direction(2.6f,sun);const Matrix m1=SunShadow(camera,sun,128,2048,basis);
        const Matrix h=SunShadow(camera,basis,128,2048);Matrix inverse;Require(Inverse(h,inverse),"held cascade inverse");
        for(float u:{-.7f,0.f,.4f})for(float v:{-.3f,.6f}){const float4v w=Project(inverse,u,v,.5f);
            const float4v q0=Project(m0,w.x,w.y,w.z),q1=Project(m1,w.x,w.y,w.z);
            Require(fabsf(q0.x-q1.x)<2e-5f && fabsf(q0.y-q1.y)<2e-5f,"centre-depth receivers keep their shadow texel");}}
    // No basis: the original E1.9 matrix, bit for bit.
    {Direction(1,sun);const Matrix a=SunShadow(camera,sun,128,2048),b=SunShadow(camera,sun,128,2048,NULL);
        Require(memcmp(a.m,b.m,sizeof(a.m))==0,"absent basis keeps the E1.9 cascade");}
    // P0-S4 flat ground clutter: leaves (3 cm) never cast sun shadows, grass (40 cm) does.
    {kich_r4_detail::Mesh leaf,grass,unknown;
        auto shape=[](kich_r4_detail::Mesh& m,float h){m.vertices.resize(2);for(unsigned i=0;i<2;++i){
            KichR4DetailVertex& v=m.vertices[i];memset(&v,0,sizeof(v));v.position[0]=i*.12f;v.position[1]=i*h;v.position[2]=i*.08f;}m.CalculateBounds();};
        shape(leaf,.03f);shape(grass,.4f);
        Require(kich_r4_detail::FlatGroundClutter(leaf) && !kich_r4_detail::FlatGroundClutter(grass),"flat leaves excluded, grass kept");
        Require(!kich_r4_detail::FlatGroundClutter(unknown),"unknown bounds keep casting");}
    std::printf("P0-S3/S4 held sun, shear, flat clutter: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
