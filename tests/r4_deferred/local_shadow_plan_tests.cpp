// P0-S7 local light shadow plan: visible faces, R2 per-light size, budget.
#include "KichR4LocalShadowPlan.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace kich_r4_local_shadow;
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static KichR4LocalLight Point(float x,float y,float z,float range,float bright=1){
    KichR4LocalLight l={};l.position[0]=x;l.position[1]=y;l.position[2]=z;l.range=range;l.type=1;l.color[0]=l.color[1]=l.color[2]=bright;return l;}
static bool Hidden(const float*,const float*,const float*){return false;}
int main(){try{
    using namespace kich_r4_lighting;
    // Camera at the origin looking down +Z, 90 degree square frustum.
    const float camera[3]={0,0,0};
    const Matrix vp=kich_r4_scene::Multiply(StableView(Vec3{0,0,0},Vec3{0,0,1}),Projection(1.57079632679f,.2f,300));
    // Light right of the view: its +X face (x 12..18) lies beyond the right plane, -X does not.
    {const uint32_t mask=FaceMask(Point(12,0,10,6),camera,vp.m,NULL);
        Require(!(mask&1) && (mask&2),"face pointing out of the frustum is culled, face toward it kept");}
    // In front of the camera every face can hold a visible receiver.
    Require(FaceMask(Point(0,0,20,5),camera,vp.m,NULL)==63,"light ahead keeps all six faces");
    // Behind the camera: the face toward the camera reaches the HUD box, the far one does not.
    {const uint32_t mask=FaceMask(Point(0,0,-10,9),camera,vp.m,NULL);
        Require((mask&16) && !(mask&32),"HUD box keeps the face toward the camera, the far face is culled");}
    Require(FaceMask(Point(0,0,-40,5),camera,vp.m,NULL)==0,"light far behind the camera renders no face");
    // HOM: an occluded face box is culled, unless the HUD box needs it.
    Require(FaceMask(Point(0,0,20,5),camera,vp.m,Hidden)==0,"occluded faces are culled");
    Require(FaceMask(Point(0,0,1,3),camera,vp.m,Hidden)==63,"faces around the camera ignore occlusion (HUD)");
    // Spot: one face along its direction.
    {KichR4LocalLight spot=Point(0,0,-10,6);spot.type=2;spot.cone=.8f;spot.direction[2]=-1;
        Require(FaceMask(spot,camera,vp.m,NULL)==0,"spot lighting only behind the camera is culled");
        spot.position[2]=10;spot.direction[2]=1;Require(FaceMask(spot,camera,vp.m,NULL)==1,"spot lighting ahead is kept");}
    Require(FaceMask(Point(0,0,-40,5),camera,NULL,NULL)==63,"no camera matrix: every face");
    // R2 compute_xf_spot: a close 8 m white light is optimal (768 -> class 1024),
    // a light 40 m beyond its sphere gets sqrt(ssa)=0.2 (153 -> 128).
    const float forward[3]={0,0,1};
    {const float close=R2Size(Point(0,0,4,8),camera,forward,1),far=R2Size(Point(0,0,48,8),camera,forward,1);
        Require(fabsf(close-768)<1 && fabsf(far-153)<2,"R2 size factors");
        Require(SizeClass(close,0)==1024 && SizeClass(far,0)==128,"power-of-two class at geometric midpoints");
        Require(R2Size(Point(0,0,4,8,0),camera,forward,1)==32,"black light gets the R2 minimum");
        Require(fabsf(R2Size(Point(0,0,4,8),camera,forward,.5f)-384)<1,"r2_ls_squality scales the size");}
    // Hysteresis: a class holds 12% beyond its midpoints.
    Require(SizeClass(760,512)==512 && SizeClass(820,512)==1024 && SizeClass(320,512)==256 && SizeClass(330,512)==512,"class hysteresis");
    Require(SizeClass(5000,0)==MaximumClass && SizeClass(1,0)==MinimumClass,"class range 64..1024");
    // Budget: three close point lights at 1024 (72 MB) -> the least important halves.
    {std::vector<uint32_t> sizes(3,1024),masks(3,63);FitBudget(sizes,masks);
        Require(sizes[0]==1024 && sizes[1]==1024 && sizes[2]==512 && masks[2]==63,"budget halves the least important largest map");}
    {std::vector<uint32_t> sizes(1000,64),masks(1000,63);FitBudget(sizes,masks);unsigned kept=0;
        for(size_t i=0;i<masks.size();++i){Require(sizes[i]==64,"never below 64 texels");if(masks[i])++kept;}
        Require(kept==682 && masks[0] && !masks[999] && Planned(sizes,masks)<=kich_r4_shadow_pool::BudgetBytes,"overflow drops the least important shadows");}
    // Face/box separation used for the HUD reach.
    {const KichR4LocalLight l=Point(0,0,0,10);const float low[3]={-1,-1,5},high[3]={1,1,6};
        Require(FaceTouchesBox(l,4,low,high) && !FaceTouchesBox(l,5,low,high) && !FaceTouchesBox(l,0,low,high),"box in front of +Z touches only that face");}
    std::printf("P0-S7 local shadow plan (faces, HUD, HOM, R2 size, hysteresis, budget): %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
