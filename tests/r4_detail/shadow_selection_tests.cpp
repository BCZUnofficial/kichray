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
void ProbeGating(){
    auto source=Source();Grid(source,12);KichR4DetailSession details;details.source=&source;kich_r4_visibility::SunCasterVolume volume;
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(77);details.BeginVisibilityFrame(77,volume);
    const Matrix views[]={Box(0,0,0,2),Box(2,0,0,3),Box(-2,0,0,4)};
    for(unsigned i=0;i<3;++i)Equivalent(details,views[i],i<2);
    const auto off=details.SelectShadow(views[2],false).instances;
    Require(details.shadow_index_builds==1 && details.shadow_index_ms==0 && details.shadow_select_ms==0 && !details.shadow_rebuild_reasons,
        "OFF performs same shadow work without diagnostic clocks/reason collection");
    ++source.engine_frame;kich_r4_record::Request(true);kich_r4_record::BeginFrame(source.engine_frame);details.BeginVisibilityFrame(source.engine_frame,volume);
    for(unsigned i=0;i<3;++i)Equivalent(details,views[i],i<2);
    Require(details.SelectShadow(views[2],false).instances==off && details.shadow_index_refits==1 && !details.shadow_rebuild_reasons,
        "recording cannot change shadow selection, reuse or rebuild policy");
    Require(std::fabs(details.shadow_index_ms-details.shadow_bounds_ms-details.shadow_bvh_build_ms-details.shadow_bvh_refit_ms)<.00001 &&
        std::fabs(details.shadow_select_ms-details.shadow_sun_select_ms-details.shadow_local_select_ms)<.00001,
        "detail attribution splits legacy totals without overlapping bounds/build/refit or sun/local");
    ++source.engine_frame;std::reverse(source.shadow_instances.begin(),source.shadow_instances.end());details.BeginVisibilityFrame(source.engine_frame,volume);
    Require((details.shadow_rebuild_reasons & KichR4DetailSession::OriginOrderChanged)!=0 && !(details.shadow_rebuild_reasons & KichR4DetailSession::CountChanged),
        "packed reorder is attributed separately from candidate count changes");
    for(unsigned i=0;i<3;++i)Equivalent(details,views[i],i<2);
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(++source.engine_frame);g_render_snapshot_active=true;
    ++source.generation;details.BeginVisibilityFrame(source.engine_frame,volume);
    Require((details.shadow_rebuild_reasons & KichR4DetailSession::SourceChanged)!=0,"explicit snapshot still captures rebuild cause while ordinary recording OFF");
    for(unsigned i=0;i<3;++i)Equivalent(details,views[i],i<2);
    g_render_snapshot_active=false;++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);
    Require(!details.shadow_rebuild_reasons && details.shadow_bounds_ms==0 && details.shadow_bvh_build_ms==0 && details.shadow_bvh_refit_ms==0 &&
        details.shadow_sun_select_ms==0 && details.shadow_local_select_ms==0,"OFF starts a clean probe after snapshot or recording ends");
}

void CellProvenance(Frame& source){
    std::map<std::pair<int,int>,uint32_t> ids;source.shadow_cells.clear();source.shadow_instance_cells.clear();
    for(const auto& instance:source.shadow_instances){
        const std::pair<int,int> key(int(std::floor(instance.row[0][3]/2)),int(std::floor(instance.row[2][3]/2)));
        auto found=ids.find(key);
        if(found==ids.end()){
            kich_r4_detail::ShadowCell cell;cell.ready=true;cell.sx=key.first;cell.sz=key.second;
            cell.center[0]=float(key.first)*2+1;cell.center[1]=0;cell.center[2]=float(key.second)*2+1;
            const uint32_t id=static_cast<uint32_t>(source.shadow_cells.size());source.shadow_cells.push_back(cell);found=ids.emplace(key,id).first;
        }
        source.shadow_instance_cells.push_back(found->second);
    }
}

void StableCells(){
    auto source=Source();Grid(source,100);CellProvenance(source);KichR4DetailSession details;details.source=&source;
    const float sun_direction[3]={.4f,-1,.3f};const auto volume=kich_r4_visibility::BuildSunCasterVolume(Box(0,0,0,8),sun_direction);
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(source.engine_frame);details.BeginVisibilityFrame(source.engine_frame,volume);
    Require(details.shadow_cell_mode,"production cell provenance selects coarse hierarchy");
    const unsigned views=32;
    for(unsigned i=0;i<views;++i)Equivalent(details,Box(float((i*17)%61)-30,0,float((i*13)%61)-30,float(1+i%5)),i<2);
    Require(details.shadow_index_builds==1 && details.shadow_order.size()==source.shadow_cells.size(),"tree partitions cache cells, not packed instance ordinals");
    Require(details.shadow_visibility_tests+details.shadow_node_tests<uint64_t(source.shadow_instances.size())*views/8,"coarse cells reduce exact narrow-view checks without a draw quota");
    const auto original_instances=source.shadow_instances;const auto original_cells=source.shadow_instance_cells;
    const double initial_build=details.shadow_bvh_build_ms;double refill_peak=0,refit_peak=0,legacy_build_peak=0;
    Frame legacy_source;KichR4DetailSession legacy;legacy.source=&legacy_source;
    // Recreate producer behavior: reverse traversal, change SSA admission and
    // migrate the same plants between wind/still buckets with changing scales.
    for(unsigned pass=1;pass<=20;++pass){
        source.shadow_instances.clear();source.shadow_instance_cells.clear();source.shadow_groups.clear();
        for(unsigned variant=0;variant<3;++variant){Group group={0,variant,static_cast<uint32_t>(source.shadow_instances.size()),0};
            for(size_t offset=0;offset<original_instances.size();++offset){const size_t i=pass%2?original_instances.size()-1-offset:offset;
                if((i+pass)%7==0 || (i+pass)%3!=variant)continue;
                auto instance=original_instances[i];const float scale=.4f+.025f*float(pass);
                for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)instance.row[row][column]*=scale;
                source.shadow_instances.push_back(instance);source.shadow_instance_cells.push_back(original_cells[i]);++group.count;}
            source.shadow_groups.push_back(group);
        }
        source.wind[1][0]=float(pass)*.1f;source.wind[2][2]=-float(pass)*.13f;++source.engine_frame;
        details.BeginVisibilityFrame(source.engine_frame,volume);
        Require(details.shadow_topology_valid && details.shadow_rebuild_reasons==0,"SSA count, group order, variant and fade changes preserve cell topology");
        for(unsigned i=0;i<views;++i)Equivalent(details,Box(float((i*17+pass)%61)-30,0,float((i*13)%61)-30,float(1+i%5)),i<2);
        KichR4LocalLight point={};point.type=1;point.range=15;point.position[0]=5;point.position[1]=2;point.position[2]=-3;
        for(unsigned face=0;face<6;++face)Equivalent(details,kich_r4_lighting::LocalShadow(point,face),false);
        point.type=2;point.direction[0]=.3f;point.direction[1]=-.2f;point.direction[2]=1;point.cone=1;
        Equivalent(details,kich_r4_lighting::LocalShadow(point,0),false);
        Require(details.shadow_index_builds==0 && details.shadow_index_refits==1,"camera repacking pays one coarse refit, never a full item rebuild");
        refill_peak=(std::max)(refill_peak,details.shadow_bounds_ms);refit_peak=(std::max)(refit_peak,details.shadow_bvh_refit_ms);
        if(pass<=3){legacy_source=source;legacy_source.shadow_cells.clear();legacy_source.shadow_instance_cells.clear();
            legacy.BeginVisibilityFrame(source.engine_frame,volume);
            for(unsigned i=0;i<3;++i)Equivalent(legacy,Box(float(i*10),0,0,4),false);
            Require(legacy.shadow_index_builds==1,"same repacked geometry reproduces old item-tree rebuild");
            legacy_build_peak=(std::max)(legacy_build_peak,legacy.shadow_bvh_build_ms);}
    }
    printf("E1.8 stable detail cells: source_items=%zu cells=%zu repacked_frames=20 initial_build_ms=%.3f bounds_peak_ms=%.3f refit_peak_ms=%.3f legacy_repack_build_peak_ms=%.3f\n",
        original_instances.size(),source.shadow_cells.size(),initial_build,refill_peak,refit_peak,legacy_build_peak);

    // Camera admission may become empty and return without replacing the cache.
    source.shadow_instances.clear();source.shadow_instance_cells.clear();source.shadow_groups.clear();++source.engine_frame;
    details.BeginVisibilityFrame(source.engine_frame,volume);Equivalent(details,Box(0,0,0,10),true);
    Require(details.shadow_topology_valid && details.shadow_index_builds==0,"empty accepted list retains ready-cache topology");
    source.shadow_instances=original_instances;source.shadow_instance_cells=original_cells;source.shadow_groups={{0,1,0,static_cast<uint32_t>(original_instances.size())}};
    ++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);Equivalent(details,Box(0,0,0,10),false);
    Require(details.shadow_index_builds==0 && details.shadow_index_refits==1,"reappearing candidates remap onto existing cell tree");

    // Unknown authoring/wind bounds fail open outside the coarse hierarchy.
    source.shadow_groups.push_back({1,2,0,2});++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);
    Equivalent(details,Box(1000,1000,1000,1),true);Require(details.SelectShadow(Box(1000,1000,1000,1),true).instances.size()==2,"unknown bounds retain overlapping group duplicates");
    source.wind[1][0]=std::numeric_limits<float>::infinity();++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);
    Equivalent(details,Box(1000,1000,1000,1),false);Require(details.shadow_index_builds==0,"known-to-unknown wind bounds do not invalidate cell structure");
    source.wind[1][0]=.5f;source.shadow_instances[0].row[0][3]=std::numeric_limits<float>::quiet_NaN();++source.engine_frame;
    details.BeginVisibilityFrame(source.engine_frame,volume);Equivalent(details,Box(1000,1000,1000,1),false);
    source.shadow_instances[0]=original_instances[0];
    auto nonfinite=Box(0,0,0,1);nonfinite.m[3]=std::numeric_limits<float>::quiet_NaN();Equivalent(details,nonfinite,false);

    // Recycled pool slot: topology is rebuilt once, membership uses only the
    // replacement frame. No old SlotItem pointer or packed index can survive.
    source.shadow_cells[3].sx+=1000;source.shadow_cells[3].center[0]+=2000;
    for(size_t i=0;i<source.shadow_instances.size();++i)if(source.shadow_instance_cells[i]==3)source.shadow_instances[i].row[0][3]+=2000;
    ++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);
    Require(!details.shadow_topology_valid && (details.shadow_rebuild_reasons & KichR4DetailSession::CellLayoutChanged),"cache slot coordinate reuse is structural invalidation");
    Equivalent(details,Box(2000,0,0,100),false);Equivalent(details,Box(0,0,0,100),true);
    Require(details.shadow_index_builds==1,"slot recycling rebuilds only the coarse cell hierarchy");

    // Optional provenance can never turn a valid geometry frame into a drop.
    source.shadow_instance_cells[0]=UINT32_MAX;++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);
    Require(!details.shadow_cell_mode,"bad cell mapping falls back to exact legacy selection");
    for(unsigned i=0;i<4;++i)Equivalent(details,Box(float(i)*5,0,0,10),false);
    source.shadow_instance_cells[0]=original_cells[0];source.shadow_cells[0].center[0]=std::numeric_limits<float>::infinity();
    ++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);Require(!details.shadow_cell_mode,"nonfinite cell anchors cannot poison hierarchy bounds");
    Equivalent(details,Box(0,0,0,10),false);
    source.Reset();++source.engine_frame;details.BeginVisibilityFrame(source.engine_frame,volume);Equivalent(details,Box(0,0,0,10),false);
    Require(!details.shadow_cell_mode && details.shadow_cells.empty() && details.shadow_nodes.empty(),"level reset retires all cell topology");
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(source.engine_frame);
}

}
int main(){try{SpatialEquivalence();BoundaryAndUnknown();Lifecycle();Benchmark();ProbeGating();StableCells();printf("E1.8 detail shadow selection: %u checks passed\n",checks);return 0;}catch(const std::exception& error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
