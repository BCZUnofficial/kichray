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
    const float sphere_inside[]={0,0,.5f},sphere_outside[]={3,0,.5f},sphere_touching[]={1.5f,0,.5f};
    Require(VisibleSphere(sphere_inside,.1f,camera),"light sphere inside camera");
    Require(!VisibleSphere(sphere_outside,.5f,camera),"non-affecting light culled by its range sphere");
    Require(VisibleSphere(sphere_touching,.5f,camera),"offscreen light whose range touches the camera remains visible");
    const Matrix perspective=kich_r4_lighting::Projection(1.57079632679f,.1f,1000.f);
    const float far_light[]={0,0,500},behind[]={0,0,-20};
    Require(VisibleSphere(far_light,5,perspective),"visible light beyond former 150m cutoff retained");
    Require(!VisibleSphere(behind,1,perspective),"light wholly behind camera rejected");
}
void LightRectangles(){using namespace kich_r4_visibility;using namespace kich_r4_lighting;
    const Matrix vp=Projection(1.57079632679f,.1f,1000.f);KichR4LocalLight light={};light.position[2]=100;light.range=5;
    const ScreenRect small=LightScreenRect(light,vp);
    Require(small.left>.45f && small.right<.55f && small.top>.45f && small.bottom<.55f,"distant source covers a small screen rectangle");
    for(unsigned ring=0;ring<=12;++ring)for(unsigned step=0;step<24;++step){
        const float phi=ring*3.14159265f/12,theta=step*6.28318531f/24;
        const float x=light.range*sinf(phi)*cosf(theta),y=light.range*cosf(phi),z=light.position[2]+light.range*sinf(phi)*sinf(theta);
        const float u=.5f+.5f*x/z,v=.5f-.5f*y/z;
        Require(u>=small.left && u<=small.right && v>=small.top && v<=small.bottom,"screen rectangle contains every sampled sphere surface projection");}
    light.position[2]=1;const ScreenRect near=LightScreenRect(light,vp);
    Require(near.left==0 && near.top==0 && near.right==1 && near.bottom==1,"near-plane crossing falls open to fullscreen");
    light.position[2]=-100;const ScreenRect behind=LightScreenRect(light,vp);
    Require(behind.left==0 && behind.top==0 && behind.right==1 && behind.bottom==1,"behind-camera descriptor falls open to fullscreen");
    light.range=std::numeric_limits<float>::quiet_NaN();const ScreenRect invalid=LightScreenRect(light,vp);
    Require(invalid.left==0 && invalid.top==0 && invalid.right==1 && invalid.bottom==1,"invalid descriptor cannot clip light coverage");
}
void SunStability(){using namespace kich_r4_lighting;
    const float direction[]={0,-1,0},camera[]={0,0,0},shifted[]={.01f,.01f,.01f};
    const Matrix base=SunShadow(camera,direction,128.f,2048),stable=SunShadow(shifted,direction,128.f,2048);
    for(unsigned i=0;i<16;++i)Require(base.m[i]==stable.m[i],"subtexel camera motion leaves sun projection unchanged");
    const float stepped[]={.0625f,0,0};const Matrix next=SunShadow(stepped,direction,128.f,2048);
    Require(fabsf(next.m[12]-base.m[12])==2.f/2048.f,"sun advances by exactly one shadow texel");
    const float receiver[]={0,0,0};const Matrix distant=SunShadow(receiver,direction,512.f,2048);
    Require(base.m[0]==distant.m[0]*4,"near sun projection improves XY resolution four times");
    for(unsigned a=0;a<4;++a)Require(base.m[a*4+2]==distant.m[a*4+2],"near and far cascades retain identical 1024m depth interval");
    const float caster_low[]={-1,199,-1},caster_high[]={1,201,1};
    Require(kich_r4_visibility::Visible(caster_low,caster_high,base) && kich_r4_visibility::Visible(caster_low,caster_high,distant),"caster 200m upstream of near receiver survives both cascade volumes");
    const float high_low[]={-1,499,-1},high_high[]={1,501,1};
    Require(kich_r4_visibility::Visible(high_low,high_high,base) && kich_r4_visibility::Visible(high_low,high_high,distant),"near cascade preserves far upstream caster coverage through 500m");
    Require(LocalShadowNear(13)==.1f && LocalShadowNear(.2f)==.05f,"local near plane is 10cm except lights shorter than 40cm use quarter range");
}
void Selection(){using namespace kich_r4_visibility;KichR4LightingFrame f={};f.count=5;f.shadow_mask=31;
    f.lights[0].type=1;f.lights[1].type=f.lights[2].type=f.lights[3].type=2;f.lights[4].type=1;
    std::vector<uint8_t> selected;SelectShadowLights(f,true,selected);
    for(unsigned i=0;i<5;++i)Require(selected[i],"all requested point and spot shadows selected");
    f.shadow_mask=(1u<<0)|(1u<<2)|(1u<<4);SelectShadowLights(f,true,selected);
    Require(selected[0] && selected[2] && selected[4] && !selected[1] && !selected[3],"individual source flags control shadows without a spot quota");
    f.shadow_mask=0;SelectShadowLights(f,true,selected);for(unsigned i=0;i<selected.size();++i)Require(!selected[i],"unshadowed sources omitted only from shadow pass");
    SelectShadowLights(f,false,selected);for(unsigned i=0;i<5;++i)Require(selected[i],"legacy bridge selection preserved");
    f.count=65;f.lights.resize(65);f.shadows.assign(65,1);f.shadows[31]=0;
    SelectShadowLights(f,true,selected);Require(selected.size()==65 && !selected[31] && selected[32] && selected[64],"shadow requests survive both old eight-light quota and 32-bit mask");
    f.sun_direction[1]=-1;Require(!SunEnabled(f),"dark sun disabled");f.sun_color[0]=.1f;Require(SunEnabled(f),"lit sun selected");
}
}
int main(){try{Frustums();LightRectangles();SunStability();Selection();printf("G3b conservative camera/light visibility: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
