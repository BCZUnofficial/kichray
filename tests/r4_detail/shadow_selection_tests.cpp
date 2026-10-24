#include "KichR4DetailSession.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
using kich_r4_detail::Frame;
using kich_r4_detail::Group;
using kich_r4_scene::Matrix;
Frame Source(){
    Frame frame;frame.ClearFrame(77);frame.generation=42;
    kich_r4_detail::Mesh mesh;mesh.vertices.resize(8);
    for(unsigned i=0;i<8;++i){auto& v=mesh.vertices[i];v={};v.position[0]=i&1?.2f:-.2f;v.position[1]=i&2?1.f:0;v.position[2]=i&4?.2f:-.2f;v.misc[2]=v.position[1];}
    mesh.CalculateBounds();frame.meshes.push_back(mesh);frame.meshes.push_back(mesh);frame.meshes[1].bounds_known=false;
    frame.wind[1][0]=.8f;frame.wind[1][2]=-.4f;frame.wind[2][0]=-.5f;frame.wind[2][2]=.9f;
    return frame;
}
KichR4DetailInstance Instance(float x,float y,float z,float scale=1){
    KichR4DetailInstance result={};result.row[0][0]=result.row[1][1]=result.row[2][2]=scale;result.row[0][3]=x;result.row[1][3]=y;result.row[2][3]=z;return result;
}
Matrix Box(float x,float y,float z,float span){
    Matrix result=kich_r4_scene::Identity();result.m[0]=result.m[5]=1.f/span;result.m[10]=.5f/span;
    result.m[12]=-x/span;result.m[13]=-y/span;result.m[14]=.5f-z*.5f/span;return result;
}
KichR4DetailSession::ShadowSelection Linear(const KichR4DetailSession& details,const Matrix& vp,bool sun){
    KichR4DetailSession::ShadowSelection result;result.valid=details.visibility_valid;
    const Frame* source=details.source;if(!source || !source->source_frame_known || source->engine_frame!=details.visibility_frame)return result;
    for(size_t i=0;i<source->shadow_groups.size();++i){const Group& g=source->shadow_groups[i];result.candidates+=g.count;if(!result.valid)continue;
        Group group={g.mesh,g.variant,static_cast<uint32_t>(result.instances.size()),0};
        for(uint32_t j=0;j<g.count;++j){const auto bounds=details.BoundsFor(source->meshes[g.mesh],source->shadow_instances[g.first+j],g.variant);
            if(bounds.known && (!kich_r4_visibility::Visible(bounds.low,bounds.high,vp) || (sun && !details.sun_casters.Visible(bounds.low,bounds.high))))continue;
            result.instances.push_back(g.first+j);++group.count;}
        if(group.count)result.groups.push_back(group);
    }
    return result;
}
void Equivalent(KichR4DetailSession& details,const Matrix& vp,bool sun){
    const auto expected=Linear(details,vp,sun);const auto& actual=details.SelectShadow(vp,sun);
    Require(expected.valid==actual.valid && expected.candidates==actual.candidates,"broadphase retains validation and candidate accounting");
    Require(expected.instances==actual.instances,"broadphase preserves exact linear selection and instance order");
    Require(expected.groups.size()==actual.groups.size(),"broadphase preserves exact group count");
    for(size_t i=0;i<expected.groups.size();++i){const Group& a=expected.groups[i];const Group& b=actual.groups[i];
        Require(a.mesh==b.mesh && a.variant==b.variant && a.first==b.first && a.count==b.count,"broadphase preserves exact group order and compact ranges");}
}
void Grid(Frame& source,unsigned width){
    for(unsigned variant=0;variant<3;++variant){const uint32_t first=static_cast<uint32_t>(source.shadow_instances.size());
        for(unsigned z=0;z<width;++z)for(unsigned x=0;x<width;++x){const float scale=(x+z)%9==0?-.7f:1.f;
            source.shadow_instances.push_back(Instance(float(x)-width*.5f,float((x*7+z*3)%5)*.1f,float(z)-width*.5f,scale));}
        const Group group={0,variant,first,width*width};source.shadow_groups.push_back(group);}
}
void SpatialEquivalence(){
    auto source=Source();Grid(source,64);KichR4DetailSession details;details.source=&source;
    const float direction[3]={.4f,-1,.3f};const auto volume=kich_r4_visibility::BuildSunCasterVolume(Box(0,0,0,8),direction);
    Require(volume.valid,"test receiver caster extrusion built");details.BeginVisibilityFrame(77,volume);
    for(unsigned i=0;i<32;++i){const auto vp=Box(float((i*17)%61)-30,float(i%5)*.1f,float((i*13)%61)-30,float(1+i%7));Equivalent(details,vp,false);Equivalent(details,vp,true);}
    KichR4LocalLight point={};point.type=1;point.range=25;point.position[0]=4;point.position[1]=3;point.position[2]=-2;
    for(unsigned face=0;face<6;++face)Equivalent(details,kich_r4_lighting::LocalShadow(point,face),false);
    point.type=2;point.direction[0]=.2f;point.direction[1]=-.3f;point.direction[2]=1;point.cone=1;
    Equivalent(details,kich_r4_lighting::LocalShadow(point,0),false);
    Equivalent(details,Box(0,0,0,1000),false);Equivalent(details,Box(1000,1000,1000,1),false);
    auto nonfinite=Box(0,0,0,1);nonfinite.m[3]=std::numeric_limits<float>::quiet_NaN();Equivalent(details,nonfinite,false);Equivalent(details,nonfinite,true);
    // Degenerate and projective-pole matrices must remain conservative.
    Matrix zero={};Equivalent(details,zero,false);auto pole=kich_r4_scene::Identity();pole.m[15]=0;pole.m[3]=1;Equivalent(details,pole,false);
    const auto index=details.shadow_index_ms,select=details.shadow_select_ms;const auto built=details.shadow_bounds_built;
    details.ResetFrame(77);Require(details.shadow_index_ms==index && details.shadow_select_ms==select && details.shadow_bounds_built==built,"upload does not erase preflight visibility metrics");
}
void BoundaryAndUnknown(){
    auto source=Source();
    for(unsigned i=0;i<128;++i)source.shadow_instances.push_back(Instance(i%2?100:-100,0,0));
    // Inside/grazing leaves must not vanish with large bounds or signed scales.
    source.shadow_instances.push_back(Instance(1.20009f,0,.5f));source.shadow_instances.push_back(Instance(-1.20009f,0,.5f));
    source.shadow_instances.push_back(Instance(2,0,.5f,1));source.shadow_instances.push_back(Instance(-2,0,.5f,-1));
    source.shadow_instances.push_back(Instance(1.e8f,0,0,-1.e6f));source.shadow_instances.push_back(Instance(0,0,.5f));
    source.shadow_instances.push_back(Instance(1000,1000,1000));
    source.shadow_instances.push_back(Instance(std::numeric_limits<float>::quiet_NaN(),0,0));
    // Out-of-order and overlapping source ranges are legal and retain duplicates.
    source.shadow_groups={{0,1,128,6},{0,0,3,125},{1,0,134,1},{0,2,135,1},{0,0,0,6},{0,1,0,0}};
    KichR4DetailSession details;details.source=&source;kich_r4_visibility::SunCasterVolume volume;details.BeginVisibilityFrame(77,volume);
    const auto vp=kich_r4_scene::Identity();Equivalent(details,Box(0,0,0,1000),false);Equivalent(details,Box(0,0,0,2000),false);Equivalent(details,vp,false);Equivalent(details,vp,true);
    Require(details.shadow_index_builds==1,"boundary/unknown regression exercises spatial traversal");
    const auto& selected=details.SelectShadow(vp,false);
    Require(std::find(selected.instances.begin(),selected.instances.end(),134)!=selected.instances.end() && std::find(selected.instances.begin(),selected.instances.end(),135)!=selected.instances.end(),"unknown authored bounds and invalid transforms fail open");
    Require(std::find(selected.instances.begin(),selected.instances.end(),130)!=selected.instances.end(),"wind-expanded off-camera leaf survives coarse rejection");
    source.wind[1][0]=std::numeric_limits<float>::infinity();source.engine_frame=78;details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);
    size_t unknown=0;for(const auto& candidate:details.shadow_bounds)unknown+=!candidate.bounds.known;
    Require(unknown>=8,"invalid wind bounds fail open without contaminating known hierarchy nodes");
}
void Lifecycle(){
    auto source=Source();Grid(source,8);KichR4DetailSession details;details.source=&source;kich_r4_visibility::SunCasterVolume volume;
    details.BeginVisibilityFrame(77,volume);const auto vp=Box(0,0,0,1);Equivalent(details,vp,false);const auto leaves=details.shadow_visibility_tests,nodes=details.shadow_node_tests;
    details.SelectShadow(vp,false);Require(details.shadow_visibility_tests==leaves && details.shadow_node_tests==nodes && details.shadow_cache_hits==1,"preflight and draw share exact result without repeated tree traversal");
    source.engine_frame=78;source.shadow_instances[0]=Instance(0,0,.5f);details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);
    Require(details.shadow_cache_hits==0,"frame update invalidates old selection and moving bounds");
    auto replacement=source;replacement.shadow_instances[0]=Instance(100,100,100);details.source=&replacement;details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);
    Require(details.visibility_source==&replacement,"same-generation source replacement invalidates tree");
    ++replacement.generation;replacement.shadow_groups={{0,0,0,1}};details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);
    Require(details.shadow_bounds_built==1 && details.shadow_views.size()==1,"generation change clears prior groups and selections");
    details.shadow_cached_bytes=16u*1024u*1024u;const auto cache_size=details.shadow_views.size();Equivalent(details,Box(100,100,100,1),false);
    Require(details.shadow_views.size()==cache_size && details.uncached_shadow.instances.size()==1,"cache budget falls back to exact uncached selection without caster quota");
    replacement.shadow_groups[0].first=UINT32_MAX;++replacement.generation;details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);
    Require(!details.visibility_valid && details.shadow_nodes.empty(),"malformed group rejected before hierarchy dereference");
    replacement.Reset();details.EnsureVisibilityFrame(78,volume);Equivalent(details,vp,false);Require(details.shadow_nodes.empty() && details.shadow_bounds.empty(),"unload cannot replay previous level candidates");
    details.source=NULL;details.EnsureVisibilityFrame(79,volume);Equivalent(details,vp,false);
}
void Benchmark(){
    auto source=Source();Grid(source,128);KichR4DetailSession details;details.source=&source;kich_r4_visibility::SunCasterVolume volume;details.BeginVisibilityFrame(77,volume);
    const unsigned views=48;uint64_t selected=0;
    for(unsigned i=0;i<views;++i){const auto vp=Box(float((i*37)%119)-60,0,float((i*29)%119)-60,3);
        Equivalent(details,vp,false);selected+=details.SelectShadow(vp,false).instances.size();}
    const uint64_t linear=uint64_t(source.shadow_instances.size())*views,total=details.shadow_visibility_tests+details.shadow_node_tests;
    Require(total<linear/8,"spatial broadphase cuts deterministic narrow-view checks by at least eight times");
    Require(details.shadow_bounds_built==source.shadow_instances.size() && details.shadow_queries==views*2 && details.shadow_cache_hits==views,"one bounds update per frame and one traversal per unique shadow view");
    Require(details.shadow_index_builds==1 && details.shadow_index_refits==0,"spatial tree built only once on third unique view");
    printf("E1.4 detail broadphase: candidates=%zu views=%u linear_checks=%llu node_checks=%llu leaf_checks=%llu selected=%llu initial_index_ms=%.3f select_ms=%.3f\n",
        source.shadow_instances.size(),views,static_cast<unsigned long long>(linear),static_cast<unsigned long long>(details.shadow_node_tests),static_cast<unsigned long long>(details.shadow_visibility_tests),static_cast<unsigned long long>(selected),details.shadow_index_ms,details.shadow_select_ms);
    ++source.engine_frame;source.wind[1][0]=2;source.wind[2][2]=3;
    for(size_t i=0;i<source.shadow_instances.size();++i){auto& instance=source.shadow_instances[i];instance.row[1][1]*=.5f;
        const float scale=instance.row[0][0];instance.row[0][0]=instance.row[2][2]=.8f*scale;instance.row[0][2]=.6f*scale;instance.row[2][0]=-.6f*scale;}
    details.EnsureVisibilityFrame(source.engine_frame,volume);Require(details.shadow_topology_valid,"same origins retain topology despite wind/scale changes");
    for(unsigned i=0;i<views;++i)Equivalent(details,Box(float((i*37)%119)-60,0,float((i*29)%119)-60,3),false);
    Require(details.shadow_index_builds==0 && details.shadow_index_refits==1,"updated wind/transform bounds refit once rather than rebuild retained tree");
    printf("E1.4 detail warm frame: refit_index_ms=%.3f select_ms=%.3f\n",details.shadow_index_ms,details.shadow_select_ms);
    ++source.engine_frame;std::reverse(source.shadow_instances.begin(),source.shadow_instances.end());details.EnsureVisibilityFrame(source.engine_frame,volume);
    Require(!details.shadow_topology_valid && details.shadow_nodes.empty(),"reordered candidate origins invalidate spatial partition");
    Equivalent(details,Box(-30,0,-30,3),false);Equivalent(details,Box(30,0,30,3),false);Equivalent(details,Box(0,0,0,3),false);
    Require(details.shadow_index_builds==1,"reordered candidates rebuild before first spatial query");
    // Ordinary sun-only frames never pay for spatial construction/refit, nor
    // sort a fully accepted output. Include a third wide view to cover the
    // linear mark/scan restoration used with an already active hierarchy.
    ++source.engine_frame;details.EnsureVisibilityFrame(source.engine_frame,volume);
    Equivalent(details,Box(0,0,0,1000),false);Equivalent(details,Box(0,0,0,2000),true);
    Require(details.shadow_index_builds==0 && details.shadow_index_refits==0 && details.shadow_node_tests==0 && !details.shadow_spatial_ready,"two wide sun views keep the original linear work without index/sort overhead");
    Require(details.shadow_visibility_tests==source.shadow_instances.size()*2,"ordinary two-view work matches old linear leaf count");
    printf("E1.4 detail two-wide-view frame: bounds_ms=%.3f select_ms=%.3f leaf_checks=%llu\n",details.shadow_index_ms,details.shadow_select_ms,static_cast<unsigned long long>(details.shadow_visibility_tests));
    Equivalent(details,Box(0,0,0,3000),false);Require(details.shadow_hit_marks.size()==source.shadow_instances.size(),"wide spatial hits restore source order with linear marks");

}
}
int main(){try{SpatialEquivalence();BoundaryAndUnknown();Lifecycle();Benchmark();printf("E1.4 detail shadow selection: %u checks passed\n",checks);return 0;}catch(const std::exception& error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
