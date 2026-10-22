#include "../../xr_3da/KichR4Visibility.h"
#include <stdio.h>
#include <stdexcept>
#include <limits>
namespace {
unsigned checks=0;
void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void Frustums(){using namespace kich_r4_visibility;using namespace kich_r4_scene;
    const Matrix camera=Identity();const float low[]={-.5f,-.5f,.1f},high[]={.5f,.5f,.9f};
    Require(Visible(low,high,camera),"inside camera");
    const float caster_low[]={3.f,-.5f,.1f},caster_high[]={4.f,.5f,.9f};
    Matrix shadow=Identity();shadow.m[12]=-3.5f;
    Require(!Visible(caster_low,caster_high,camera) && Visible(caster_low,caster_high,shadow),"off-camera caster remains a shadow candidate");
    const float crossing_low[]={-2.f,-2.f,-1.f},crossing_high[]={2.f,2.f,2.f};
    Require(Visible(crossing_low,crossing_high,camera),"intersecting box cannot be rejected by a center-only test");
    const float near_low[]={-.5f,-.5f,-1.e-6f},near_high[]={.5f,.5f,0};
    Require(Visible(near_low,near_high,camera),"near-plane tolerance");
    Bounds unknown;Require(Visible(unknown,camera),"unknown bounds conservatively retained");
    float invalid[]={std::numeric_limits<float>::quiet_NaN(),0,0};Require(Visible(invalid,high,camera),"invalid source bounds never false-negative cull");
    KichR4LocalLight light={};light.type=1;light.range=10;
    const float face_low[]={4.f,-.2f,-.2f},face_high[]={5.f,.2f,.2f};
    Require(Visible(face_low,face_high,kich_r4_lighting::LocalShadow(light,0)),"point positive-x face retains caster");
    Require(!Visible(face_low,face_high,kich_r4_lighting::LocalShadow(light,1)),"point opposite face rejects caster");
    const float outside_low[]={20.f,-.2f,-.2f},outside_high[]={21.f,.2f,.2f};
    Require(!Visible(outside_low,outside_high,kich_r4_lighting::LocalShadow(light,0)),"local-light range clips world caster");
}
void Selection(){using namespace kich_r4_visibility;KichR4LightingFrame f={};f.count=5;f.shadow_mask=31;
    f.lights[0].type=1;f.lights[1].type=f.lights[2].type=f.lights[3].type=2;f.lights[4].type=1;
    bool selected[8];SelectShadowLights(f,true,selected);
    Require(selected[1] && selected[2] && !selected[0] && !selected[3] && !selected[4],"two spots consume shadow policy");
    f.shadow_mask=(1u<<0)|(1u<<2)|(1u<<4);SelectShadowLights(f,true,selected);
    Require(selected[0] && selected[2] && !selected[1] && !selected[4],"one spot plus first permitted point");
    f.shadow_mask=0;SelectShadowLights(f,true,selected);for(unsigned i=0;i<8;++i)Require(!selected[i],"unshadowed sources omitted only from shadow pass");
    SelectShadowLights(f,false,selected);for(unsigned i=0;i<5;++i)Require(selected[i],"legacy bridge selection preserved");
    f.sun_direction[1]=-1;Require(!SunEnabled(f),"dark sun disabled");f.sun_color[0]=.1f;Require(SunEnabled(f),"lit sun selected");
}
}
int main(){try{Frustums();Selection();printf("G3b conservative camera/light visibility: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
