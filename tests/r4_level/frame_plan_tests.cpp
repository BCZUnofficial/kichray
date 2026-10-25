#include "../../xr_3da/KichR4LevelVisibility.h"
#include <stdio.h>
#include <limits>
#include <cstdlib>
#include <new>
static bool count_allocations=false;
static size_t allocations=0;
#ifdef _MSC_VER
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(size_t n){if(count_allocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
using namespace kich_r4_level;
namespace {
unsigned checks=0;
void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
uint32_t random_state=1234567;
float Random(float low,float high){random_state=random_state*1664525u+1013904223u;return low+(high-low)*float(random_state>>8)/16777216.f;}
Draw Box(float x,float y,float z,float radius){Draw d={};d.world=kich_r4_scene::Identity();d.low={x-radius,y-radius,z-radius};d.high={x+radius,y+radius,z+radius};return d;}
void Equivalent(const Scene& scene,StaticFramePlan& plan,const Matrix& vp,uint32_t sector,float wx,float wz){
    StaticVisibility baseline;SelectStatic(scene,vp,sector,wx,wz,baseline);
    const StaticVisibility& selected=plan.Select(vp,sector);
    Require(selected.draws==baseline.draws,"BVH selection must equal conservative flat selection");
    Require(selected.selected==baseline.selected,"selected draw order must preserve original opaque order");
    Require(selected.sectors==baseline.sectors && selected.portal_tests==baseline.portal_tests && selected.sector_rejected==baseline.sector_rejected && selected.frustum_rejected==baseline.frustum_rejected,"selection counters remain exact");
    const uint64_t tests=plan.bounds_tests,nodes=plan.node_tests;const uint32_t hits=plan.cache_hits;
    Require(&plan.Select(vp,sector)==&selected && plan.bounds_tests==tests && plan.node_tests==nodes && plan.cache_hits==hits+1,"residency/submission reuse result without repeating traversal");
}
void Spatial(){Scene s;s.materials.resize(1);s.vertices.resize(1);s.materials[0].descriptor.tree_wave=true;s.vertices[0].tree_fraction_max=1;
    for(unsigned i=0;i<4096;++i){Draw d=Box(Random(-1000,1000),Random(-50,50),Random(-1000,1000),Random(.01f,8));d.tree=i%13==0;s.draws.push_back(d);}
    StaticFramePlan plan;plan.Begin(s,1,.5f,-.3f);
    for(unsigned i=0;i<48;++i){const Vec3 eye={Random(-500,500),Random(-10,20),Random(-500,500)},target={eye.x+Random(-1,1),eye.y+Random(-.3f,.3f),eye.z+1};
        const Matrix vp=kich_r4_scene::Multiply(kich_r4_scene::LookAt(eye,target),kich_r4_scene::Perspective(1.7f));Equivalent(s,plan,vp,UINT32_MAX,.5f,-.3f);}
    plan.Begin(s,1,0,0);const StaticVisibility& tiny=plan.Select(kich_r4_scene::Identity(),UINT32_MAX);
    Require(tiny.bounds_tests<s.draws.size()/8,"narrow view prunes whole BVH branches before leaf tests");
    Require(plan.views.size()==1 && plan.cache_hits==0,"new frame discards prior view results");
    s.draws.clear();s.draws.push_back(Box(0,0,.5f,.1f));plan.Begin(s,2,0,0);Equivalent(s,plan,kich_r4_scene::Identity(),UINT32_MAX,0,0);
    Require(plan.order.size()==1 && plan.nodes.size()==1,"new level revision rebuilds spatial index");
    s.draws.clear();plan.Begin(s,3,0,0);Equivalent(s,plan,kich_r4_scene::Identity(),UINT32_MAX,0,0);
}
void PortalsAndWind(){Scene s;s.materials.resize(1);s.vertices.resize(1);s.materials[0].descriptor.tree_wave=true;s.vertices[0].tree_fraction_max=1;
    s.topology.valid=true;s.topology.sectors.resize(3);s.sector_owned={1,1,1,0};
    for(unsigned i=0;i<4;++i){s.draws.push_back(Box(0,0,.5f,.1f));if(i<3)s.topology.sectors[i].draws.push_back(i);}
    Portal p;p.front=0;p.back=1;p.vertices={{-.1f,-.1f,.2f},{.1f,-.1f,.2f},{.1f,.1f,.2f},{-.1f,.1f,.2f}};
    s.topology.portals.push_back(p);s.topology.sectors[0].portals={0};s.topology.sectors[1].portals={0};
    StaticFramePlan plan;plan.Begin(s,1,0,0);for(unsigned sector=0;sector<3;++sector)Equivalent(s,plan,kich_r4_scene::Identity(),sector,0,0);
    Equivalent(s,plan,kich_r4_scene::Identity(),UINT32_MAX,0,0);
    s.draws[0]=Box(3,3,.5f,.1f);s.draws[0].tree=true;plan.Begin(s,1,0,0);
    Require(!plan.Select(kich_r4_scene::Identity(),UINT32_MAX).draws[0],"stationary out-of-frustum tree rejected");
    s.draws[0].low.y=0;s.draws[0].high.y=3;plan.Begin(s,1,1,0);Equivalent(s,plan,kich_r4_scene::Identity(),UINT32_MAX,1,0);
    Require(plan.Select(kich_r4_scene::Identity(),UINT32_MAX).draws[0],"same topology refits wind bounds each frame");
    s.draws[1].low.x=std::numeric_limits<float>::quiet_NaN();plan.Begin(s,1,0,0);
    Require(plan.Select(kich_r4_scene::Identity(),UINT32_MAX).draws[1],"unknown bounds cannot be hidden by BVH ancestors");
}
void Sun(){Scene s;s.materials.resize(1);s.draws.push_back(Box(0,0,.5f,.1f));s.draws.push_back(Box(20,0,.5f,.1f));
    StaticFramePlan plan;plan.Begin(s,1,0,0);const float direction[3]={0,0,1};plan.sun_casters=kich_r4_visibility::BuildSunCasterVolume(kich_r4_scene::Identity(),direction);
    Matrix shadow=kich_r4_scene::Identity();shadow.m[0]=shadow.m[5]=.01f;
    const StaticVisibility& local=plan.Select(shadow,UINT32_MAX,false);Require(local.selected.size()==2,"local volume retains both casters");
    const StaticVisibility& sun=plan.Select(shadow,UINT32_MAX,true);Require(sun.selected.size()==1 && sun.selected[0]==0,"sun extrusion removes caster unable to reach camera receivers");
    Require(plan.views.size()==2 && &sun!=&local,"identical shadow matrix has distinct sun and local cache entries");
    plan.Begin(s,1,0,0);plan.sun_casters=kich_r4_visibility::SunCasterVolume();Require(plan.Select(shadow,UINT32_MAX,true).selected.size()==2,"invalid sun volume fails open");
}
// P0-S8 cascade holes: the 512 m view skips casters inside 0.88 of the 128 m
// cascade (a pixel there gives the 512 m map zero weight); casters reaching
// past it, other views and frames without a registered hole are unchanged.
void Holes(){Scene s;s.materials.resize(1);
    const float xs[]={0,20,40,55,56.5f,57.5f,62,70,150,240};
    for(float x:xs)s.draws.push_back(Box(x,0,0,.5f));
    s.draws.push_back(Box(30,0,0,30)); // spans 0..60 m: reaches past the hole
    StaticFramePlan plan;plan.Begin(s,1,0,0);
    const float origin[3]={0,0,0},down[3]={.1f,-1,.05f};
    const Matrix near_view=kich_r4_lighting::SunShadow(origin,down,128,4096),far_view=kich_r4_lighting::SunShadow(origin,down,512,8192);
    const size_t without=plan.Select(far_view,UINT32_MAX,true).selected.size(),near_without=plan.Select(near_view,UINT32_MAX,true).selected.size();
    Require(without==s.draws.size(),"without a hole the 512 m view keeps every caster");
    plan.Begin(s,1,0,0);plan.AddSunHole(far_view,near_view);
    const StaticVisibility& far=plan.Select(far_view,UINT32_MAX,true);
    for(size_t i=0;i<s.draws.size();++i){bool inside=true;
        for(unsigned c=0;c<8;++c){const float p[3]={c&1?s.draws[i].high.x:s.draws[i].low.x,c&2?s.draws[i].high.y:s.draws[i].low.y,c&4?s.draws[i].high.z:s.draws[i].low.z};
            float q[4];for(unsigned a=0;a<4;++a)q[a]=p[0]*near_view.m[a]+p[1]*near_view.m[4+a]+p[2]*near_view.m[8+a]+near_view.m[12+a];
            inside=inside && std::fabs(q[0])<=.88f*q[3] && std::fabs(q[1])<=.88f*q[3];}
        Require(far.draws[i]==(inside?0:1),"512 m view drops exactly the casters inside 0.88 of the 128 m cascade");}
    Require(far.draws[0]==0 && far.draws[3]==0 && far.draws[7]==1 && far.draws[8]==1 && far.draws.back()==1,"near casters dropped, far and straddling casters kept");
    Require(far.hole_rejected==std::count(far.draws.begin(),far.draws.end(),0),"hole rejections are counted");
    Require(plan.Select(near_view,UINT32_MAX,true).selected.size()==near_without,"the 128 m view itself has no hole");
    Require(plan.Select(far_view,UINT32_MAX,false).selected.size()==s.draws.size(),"a local (non-sun) query with the same matrix ignores the hole");
    // Contact hole with its own centre (r2_sun_near sphere, not the camera):
    // the test is XY-only, also for a caster ~400 m along the sun ray, which
    // lies outside the contact depth window. SunVisibility (P0-S9) gives the
    // contact map every pixel of its XY square at any depth, so such casters'
    // receivers never read the 128 m map (shadow_filter_tests CascadeSelection).
    {Scene z;z.materials.resize(1);const float centre[3]={6,0,-4};const Matrix contact=kich_r4_lighting::SunShadow(centre,down,40,2048);
        const float t=400;z.draws.push_back(Box(6+down[0]*t,down[1]*t,-4+down[2]*t,.5f));z.draws.push_back(Box(6,0,-4,.5f));
        plan.Begin(z,3,0,0);plan.AddSunHole(near_view,contact);const StaticVisibility& v=plan.Select(near_view,UINT32_MAX,true);
        Require(!v.draws[0] && !v.draws[1] && v.hole_rejected==2,"contact hole is XY-only, at any depth along the ray");}
    // Many casters: whole BVH branches inside the hole are skipped.
    Scene many;many.materials.resize(1);for(unsigned i=0;i<4096;++i)many.draws.push_back(Box(Random(-45,45),Random(-5,5),Random(-45,45),Random(.1f,2)));
    plan.Begin(many,2,0,0);plan.AddSunHole(far_view,near_view);
    const StaticVisibility& dense=plan.Select(far_view,UINT32_MAX,true);
    Require(dense.selected.empty() && dense.hole_rejected==many.draws.size() && dense.bounds_tests<many.draws.size()/4,"dense near casters are skipped by branch");
}
void Reuse(){Scene s;s.materials.resize(1);s.topology.valid=true;s.topology.sectors.resize(2);
    s.draws.resize(2048);s.sector_owned.assign(s.draws.size(),1);
    for(size_t i=0;i<s.draws.size();++i){s.draws[i]=Box(float(i%4)*.2f,0,.5f,.05f);s.topology.sectors[i%2].draws.push_back(static_cast<uint32_t>(i));}
    Portal p;p.front=0;p.back=1;p.vertices={{-.1f,-.1f,.2f},{.1f,-.1f,.2f},{.1f,.1f,.2f},{-.1f,.1f,.2f}};
    s.topology.portals.push_back(p);s.topology.sectors[0].portals.push_back(0);s.topology.sectors[1].portals.push_back(0);
    StaticFramePlan plan;plan.Begin(s,1,0,0);const Matrix identity=kich_r4_scene::Identity();
    const StaticVisibility& first=plan.Select(identity,0);const unsigned char* mask=first.draws.data();const uint32_t* indices=first.selected.data();
    std::vector<Matrix> matrices(48,identity);
    for(size_t i=0;i<matrices.size();++i){matrices[i].m[12]=float(i)*.01f;plan.Select(matrices[i],0);}
    Require(&first==&plan.Select(identity,0) && first.draws.data()==mask && first.selected.data()==indices,"adding view slots cannot invalidate a returned camera result");
    allocations=0;count_allocations=true;plan.Begin(s,1,0,0);
    for(size_t i=0;i<matrices.size();++i)plan.Select(matrices[i],0);
    count_allocations=false;
    Require(allocations==0,"warmed frame performs no BVH, per-view mask, index, key or portal scratch allocations");
    Require(plan.view_slots.front().draws.data()==mask && plan.view_slots.front().selected.data()==indices,"warm frames retain buffer storage");
    s.draws[0]=Box(1000,1000,.5f,.1f);plan.Begin(s,1,0,0);
    Require(!plan.Select(identity,0).draws[0],"retained capacity never retains stale visibility decisions");
    const StaticVisibility* stable=&plan.Select(identity,0);
    const size_t per_view=sizeof(StaticVisibility)+sizeof(StaticFramePlan::Key)+s.draws.size()*5;
    const size_t limit=(std::max)(size_t(1),size_t(16u*1024u*1024u)/per_view);
    Matrix far=identity;far.m[12]=10000;
    for(size_t i=0;i<limit+8;++i){far.m[13]=float(i);plan.Select(far,0);}
    Require(plan.view_slots.size()==limit && plan.views.size()==limit,"view storage stays within the existing cache budget");
    Require(&plan.Select(identity,0)==stable && !stable->draws[0],"cache overflow does not move or overwrite cached references");
    s.draws.resize(1);s.sector_owned.clear();plan.Begin(s,2,0,0);
    Require(plan.view_slots.empty() && plan.views.empty() && plan.allowed.empty(),"level revision releases prior level high-water scratch");
}
}
int main(){try{Spatial();PortalsAndWind();Sun();Holes();Reuse();printf("E1.3/E1.6 shared static frame plan: %u checks passed\n",checks);return 0;}catch(const std::exception& e){count_allocations=false;fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
