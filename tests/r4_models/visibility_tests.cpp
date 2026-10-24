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
void SunCasters(){using namespace kich_r4_visibility;using namespace kich_r4_lighting;
    const float down[]={0,-1,0};const SunCasterVolume box=BuildSunCasterVolume(Identity(),down);
    Require(box.valid && box.count<=14,"orthographic receiver caster volume has bounded plane storage");
    const float upstream_low[]={-.1f,199,.4f},upstream_high[]={.1f,201,.6f};
    Require(!Visible(upstream_low,upstream_high,Identity()) && box.Visible(upstream_low,upstream_high),"upstream caster outside camera still shadows a receiver");
    const float downstream_low[]={-.1f,-3,.4f},downstream_high[]={.1f,-2,.6f};
    Require(!box.Visible(downstream_low,downstream_high),"downstream object cannot shadow any camera receiver");
    const float lateral_low[]={2,199,.4f},lateral_high[]={3,201,.6f};
    Require(!box.Visible(lateral_low,lateral_high),"sun silhouette rejects lateral geometry unrelated to visible receivers");
    const float touching[]={1.5f,20,.5f};Require(box.VisibleSphere(touching,.5f),"sphere touching silhouette is conservatively retained");
    const float outside[]={3,20,.5f};Require(!box.VisibleSphere(outside,.5f),"sphere wholly outside sun footprint rejected");

    // Every point in the camera frustum and every upstream point on its light
    // ray must survive. Cover oblique/low sun, frustum boundaries, near/far
    // disparity, camera rotation and large translated world coordinates.
    const kich_r4_scene::Vec3 eyes[]={{0,0,0},{231,-18,149},{100000,200,-100000}};
    const kich_r4_scene::Vec3 directions[]={{0,0,1},{1,.2f,-.7f},{-.8f,-.3f,.2f}};
    const float suns[][3]={{0,-1,0},{1,-.001f,.3f},{0,0,1},{0,0,-1},{-.37f,-.51f,.77f}};
    const double distances[]={0,1,200,500,2000};
    for(unsigned camera=0;camera<3;++camera){const Matrix vp=Multiply(StableView(eyes[camera],directions[camera]),Projection(1.1f,.1f,640));
        // Independent adjugate unprojection: do not lose the far plane to the
        // float inverse used by the legacy constants path, and do not repeat
        // the production Gaussian elimination algorithm in this oracle.
        double adjugate[4][4];
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row){double minor[3][3];unsigned y=0;
            for(unsigned r=0;r<4;++r)if(r!=col){unsigned x=0;for(unsigned c=0;c<4;++c)if(c!=row)minor[y][x++]=vp.m[r*4+c];++y;}
            const double determinant=minor[0][0]*(minor[1][1]*minor[2][2]-minor[1][2]*minor[2][1])-minor[0][1]*(minor[1][0]*minor[2][2]-minor[1][2]*minor[2][0])+minor[0][2]*(minor[1][0]*minor[2][1]-minor[1][1]*minor[2][0]);
            adjugate[row][col]=((row+col)&1)?-determinant:determinant;
        }
        for(unsigned s=0;s<5;++s){const SunCasterVolume volume=BuildSunCasterVolume(vp,suns[s]);
            Require(volume.valid && volume.count<=14,"perspective sun caster volume built");
            const kich_r4_scene::Vec3 sun=kich_r4_scene::Normalize(kich_r4_scene::Vec3{suns[s][0],suns[s][1],suns[s][2]});
            for(unsigned ix=0;ix<5;++ix)for(unsigned iy=0;iy<5;++iy)for(unsigned iz=0;iz<5;++iz){
                const double clip[4]={double(ix)*.5-1,double(iy)*.5-1,double(iz)*.25,1};double world[4]={};
                for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)world[a]+=clip[b]*adjugate[b][a];
                for(unsigned d=0;d<5;++d){const float direction[3]={sun.x,sun.y,sun.z};float point[3],low[3],high[3];
                    for(unsigned a=0;a<3;++a){point[a]=static_cast<float>(world[a]/world[3]-direction[a]*distances[d]);low[a]=point[a]-.01f;high[a]=point[a]+.01f;}
                    Require(volume.Visible(low,high),"receiver ray caster survives the extruded volume");
                    Require(volume.VisibleSphere(point,.01f),"receiver ray sphere survives the extruded volume");}
            }
        }
    }
    Matrix singular={};Require(!BuildSunCasterVolume(singular,down).valid,"singular camera falls open");
    Matrix infinite=Projection(1.f,.1f,100);infinite.m[10]=1;Require(!BuildSunCasterVolume(infinite,down).valid,"infinite camera projection falls open");
    const float zero[]={0,0,0};Require(!BuildSunCasterVolume(Identity(),zero).valid,"zero sun direction falls open");
    const float invalid[]={std::numeric_limits<float>::quiet_NaN(),0,0};
    Require(!BuildSunCasterVolume(Identity(),invalid).valid && box.Visible(invalid,upstream_high),"invalid sun and bounds cannot hide geometry");
    Require(SunCasterVolume().Visible(lateral_low,lateral_high),"uninitialized volume cannot cull");
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
int main(){try{Frustums();LightRectangles();SunStability();SunCasters();Selection();printf("G3b/E1.3 conservative camera/light visibility: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
