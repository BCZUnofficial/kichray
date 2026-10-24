#ifndef KICH_R4_DETAIL_SESSION_H
#define KICH_R4_DETAIL_SESSION_H
#include "KichR4DetailData.h"
#include "KichR4LevelSession.h"
#include "KichR4LightingMath.h"
#include "KichR4ShadowRecord.h"
#include "KichR4Visibility.h"
#include "KichR4FrameMetrics.h"
#include "RenderSnapshot.h"
#include <array>
#include <map>
#include <utility>
struct KichR4DetailSession {
    struct Mesh {KichR4Handle vb,ib,texture;uint32_t indices;Mesh():vb(0),ib(0),texture(0),indices(0){}};
    KichR4DetailAPI api;
    const kich_r4_detail::Frame* source;
    std::vector<Mesh> meshes;
    KichR4Handle instance_buffer,shadow_instance_buffer,vs,pipelines[2];
    uint64_t generation,vertex_bytes,index_bytes;
    uint32_t capacity,shadow_capacity,uploaded_frame,draw_calls,shadow_draw_calls;
    uint32_t telemetry_frame,uploaded_instances;
    uint64_t submitted_instances,shadow_submitted_instances;
    uint64_t shadow_upload_bytes,shadow_candidates,shadow_culled;
    std::vector<KichR4DetailInstance> selected_instances;
    std::vector<kich_r4_detail::Group> selected_groups;
    KichR4DetailSession():source(NULL),instance_buffer(0),shadow_instance_buffer(0),vs(0),generation(UINT64_MAX),vertex_bytes(0),index_bytes(0),capacity(0),shadow_capacity(0),uploaded_frame(UINT32_MAX),draw_calls(0),shadow_draw_calls(0),telemetry_frame(UINT32_MAX),uploaded_instances(0),submitted_instances(0),shadow_submitted_instances(0),shadow_upload_bytes(0),shadow_candidates(0),shadow_culled(0){memset(&api,0,sizeof(api));pipelines[0]=pipelines[1]=0;}
    static std::string MaterialKey(const std::string& identity){
        uint64_t hash=UINT64_C(14695981039346656037);for(size_t i=0;i<identity.size();++i){hash^=static_cast<unsigned char>(identity[i]);hash*=UINT64_C(1099511628211);}
        char key[64];snprintf(key,sizeof(key),"material/fnv1a64/%016llx",static_cast<unsigned long long>(hash));return key;
    }
    void ResetFrame(uint32_t frame){telemetry_frame=frame;uploaded_instances=draw_calls=shadow_draw_calls=0;submitted_instances=shadow_submitted_instances=shadow_upload_bytes=shadow_candidates=shadow_culled=0;}
    void SnapshotSummary(uint32_t frame,bool scene)const{
        if(!g_render_snapshot_active)return;
        try{
        const bool current=scene && source && source->source_frame_known && source->engine_frame==frame;
        const bool attempted=scene && telemetry_frame==frame;
        const char* status=!scene?"SCENE_NOT_SUBMITTED":!source?"UNAVAILABLE":!current?"UNPREPARED_FOR_FRAME":source->instances.empty()?(source->shadow_instances.empty()?"CURRENT_EMPTY":"CURRENT_SHADOW_ONLY"):attempted && uploaded_frame==frame?"CURRENT_UPLOADED":"CURRENT_NOT_UPLOADED";
        RenderSnapshot_Text("DETAIL_RENDERER","status",status);RenderSnapshot_Text("DETAIL_RENDERER","path","NATIVE_DX11");
        RenderSnapshot_Text("DETAIL_RENDERER","instance_scope","CAMERA_SELECTED_LIST; NOT_VISIBLE_PIXELS");
        RenderSnapshot_Text("DETAIL_RENDERER","shadow_selection","PER_SHADOW_FRUSTUM; READY_CACHE_WITHIN_DETAIL_RADIUS; WIND_EXPANDED_BOUNDS; FRAME_SPATIAL_BVH");
        const bool source_frame_known=source && source->source_frame_known;
        RenderSnapshot_U64("DETAIL_RENDERER","source_frame_valid",source_frame_known?1:0);
        if(source_frame_known){RenderSnapshot_U64("DETAIL_RENDERER","source_engine_frame",source->engine_frame);
            if(frame>=source->engine_frame)RenderSnapshot_U64("DETAIL_RENDERER","source_age_engine_frames",uint64_t(frame)-source->engine_frame);
            else RenderSnapshot_Text("DETAIL_RENDERER","source_age_engine_frames","FRAME_COUNTER_WRAP_OR_RESET");}
        else{RenderSnapshot_Text("DETAIL_RENDERER","source_engine_frame","NOT_AVAILABLE");RenderSnapshot_Text("DETAIL_RENDERER","source_age_engine_frames","NOT_AVAILABLE");}
        if(source)RenderSnapshot_U64("DETAIL_RENDERER","source_generation",source->generation);
        RenderSnapshot_Text("DETAIL_RENDERER","visibility_pixels","NOT_MEASURED");
        RenderSnapshot_Text("DETAIL_RENDERER","source_bound_method","DECODED_LOCAL_MESH_AABB; INSTANCE_TRANSFORM; WIND_ENVELOPE");
        RenderSnapshot_Text("DETAIL_RENDERER","shadow_portal_hom","NOT_USED; READY_CACHE_AND_FRUSTUM_ONLY");
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_source_instances",current?source->shadow_instances.size():0);
        RenderSnapshot_U64("DETAIL_RENDERER","visible_instances",current?source->instances.size():0);
        RenderSnapshot_U64("DETAIL_RENDERER","still_instances",current?source->variants[0]:0);RenderSnapshot_U64("DETAIL_RENDERER","wind1_instances",current?source->variants[1]:0);RenderSnapshot_U64("DETAIL_RENDERER","wind2_instances",current?source->variants[2]:0);
        RenderSnapshot_U64("DETAIL_RENDERER","loaded_meshes",source?source->meshes.size():0);RenderSnapshot_U64("DETAIL_RENDERER","unique_meshes",source?source->meshes.size():0);
        size_t active=0;if(current && RenderSnapshot_Detailed()){std::vector<bool> seen(source->meshes.size(),false);for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];if(g.count && g.mesh<seen.size() && !seen[g.mesh]){seen[g.mesh]=true;++active;}}}
        if(RenderSnapshot_Detailed())RenderSnapshot_U64("DETAIL_RENDERER","active_meshes",active);else RenderSnapshot_Text("DETAIL_RENDERER","active_meshes","NOT_MEASURED_COUNTER_MODE");RenderSnapshot_U64("DETAIL_RENDERER","groups",current?source->groups.size():0);
        const bool mesh_memory_known=!source || generation==source->generation || (!vertex_bytes && !index_bytes);
        RenderSnapshot_Text("DETAIL_RENDERER","mesh_memory_status",mesh_memory_known?"TRACKED_ALLOCATION":"GENERATION_UPLOAD_INCOMPLETE");
        if(mesh_memory_known){RenderSnapshot_U64("DETAIL_RENDERER","gpu_vertex_bytes",vertex_bytes);RenderSnapshot_U64("DETAIL_RENDERER","gpu_index_bytes",index_bytes);}
        else{RenderSnapshot_Text("DETAIL_RENDERER","gpu_vertex_bytes","NOT_AVAILABLE");RenderSnapshot_Text("DETAIL_RENDERER","gpu_index_bytes","NOT_AVAILABLE");}
        RenderSnapshot_U64("DETAIL_RENDERER","instance_capacity",capacity);RenderSnapshot_U64("DETAIL_RENDERER","instance_allocated_bytes",uint64_t(capacity)*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("DETAIL_RENDERER","instance_upload_bytes",attempted?uint64_t(uploaded_instances)*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_capacity",shadow_capacity);RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_allocated_bytes",uint64_t(shadow_capacity)*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_upload_bytes",attempted?shadow_upload_bytes:0);
        const bool visibility_current=current && visibility_frame==frame && visibility_source==source && visibility_generation==source->generation;
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_bounds_built",visibility_current?shadow_bounds_built:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_node_tests",visibility_current?shadow_node_tests:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_index_builds",visibility_current?shadow_index_builds:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_index_refits",visibility_current?shadow_index_refits:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_leaf_tests",visibility_current?shadow_visibility_tests:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_selection_queries",visibility_current?shadow_queries:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_selection_cache_hits",visibility_current?shadow_cache_hits:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_shadow_index_ms",visibility_current?shadow_index_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_shadow_select_ms",visibility_current?shadow_select_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_shadow_bounds_ms",visibility_current?shadow_bounds_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_shadow_bvh_build_ms",visibility_current?shadow_bvh_build_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_shadow_bvh_refit_ms",visibility_current?shadow_bvh_refit_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_sun_selection_ms",visibility_current?shadow_sun_select_ms:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_local_selection_ms",visibility_current?shadow_local_select_ms:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_rebuild_reason_bits",visibility_current?shadow_rebuild_reasons:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_candidate_observations",attempted?shadow_candidates:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_culled_observations",attempted?shadow_culled:0);
        RenderSnapshot_U64("DETAIL_RENDERER","instanced_draw_calls",attempted?draw_calls:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_draw_calls",attempted?shadow_draw_calls:0);
        RenderSnapshot_U64("DETAIL_RENDERER","geometry_submitted_instances",attempted?submitted_instances:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_submitted_instances",attempted?shadow_submitted_instances:0);
        RenderSnapshot_U64("DETAIL_RENDERER","legacy_capture_vertices",source && source->engine_frame==frame?source->legacy_vertices:0);RenderSnapshot_U64("DETAIL_RENDERER","legacy_capture_indices",source && source->engine_frame==frame?source->legacy_indices:0);RenderSnapshot_U64("DETAIL_RENDERER","legacy_detail_draws",source && source->engine_frame==frame?source->legacy_draws:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_prepare_ms",current?source->prepare_ms:0);RenderSnapshot_Text("DETAIL_RENDERER","gpu_detail_ms","NOT_INSTRUMENTED");
        RenderSnapshot_U64("MEMORY","detail_cpu_instance_payload_bytes",source?source->instances.size()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_instance_capacity_bytes",source?source->instances.capacity()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_candidate_payload_bytes",source?source->shadow_instances.size()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_candidate_capacity_bytes",source?source->shadow_instances.capacity()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_selected_payload_bytes",selected_instances.size()*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_selected_capacity_bytes",selected_instances.capacity()*sizeof(KichR4DetailInstance));
        uint64_t mesh_payload=0;if(source)for(size_t i=0;i<source->meshes.size();++i){const kich_r4_detail::Mesh& mesh=source->meshes[i];mesh_payload+=mesh.vertices.size()*sizeof(KichR4DetailVertex)+mesh.indices.size()*sizeof(uint16_t);}
        RenderSnapshot_U64("MEMORY","detail_cpu_mesh_payload_bytes",mesh_payload);
        RenderSnapshot_U64("MEMORY","detail_cpu_group_payload_bytes",source?source->groups.size()*sizeof(kich_r4_detail::Group):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_group_payload_bytes",source?source->shadow_groups.size()*sizeof(kich_r4_detail::Group):0);
        RenderSnapshot_Text("MEMORY","detail_cpu_instance_scope","RETAINED_FRONTEND_STORAGE; MAY_BE_FROM_PRIOR_FRAME");
        }catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","r4_detail_summary","ALLOCATION_FAILED; PARTIAL_SUMMARY");}
    }
    struct ShadowBounds {float low[3],high[3];bool known;ShadowBounds():known(false){memset(low,0,sizeof(low));memset(high,0,sizeof(high));}};
    struct ShadowSelection {std::vector<uint32_t> instances;std::vector<kich_r4_detail::Group> groups;uint64_t candidates=0;bool valid=true;};
    typedef std::array<uint32_t,17> ShadowViewKey;
    struct ShadowCandidate {ShadowBounds bounds;uint32_t instance,group;float origin[3];};
    struct ShadowNode {ShadowBounds bounds;uint32_t first,count,left,right;};
    // Candidates retain original group/instance order. Only the index is
    // spatially partitioned; sorting query hits restores submission order.
    std::vector<ShadowCandidate> shadow_bounds;
    std::vector<uint32_t> shadow_order,shadow_unknown,shadow_hits;
    std::vector<ShadowNode> shadow_nodes;
    std::vector<unsigned char> shadow_hit_marks;
    bool shadow_topology_valid=false,shadow_spatial_ready=false;
    uint64_t shadow_source_candidates=0;
    std::map<ShadowViewKey,ShadowSelection> shadow_views;
    ShadowSelection uncached_shadow;
    kich_r4_visibility::SunCasterVolume sun_casters;
    const kich_r4_detail::Frame* visibility_source=NULL;
    uint64_t visibility_generation=UINT64_MAX;
    uint32_t visibility_frame=UINT32_MAX;
    bool visibility_valid=true;
    uint64_t shadow_bounds_built=0,shadow_visibility_tests=0,shadow_cache_hits=0,shadow_cached_bytes=0;
    uint64_t shadow_node_tests=0,shadow_queries=0;
    uint32_t shadow_index_builds=0,shadow_index_refits=0;
    double shadow_index_ms=0,shadow_select_ms=0;
    // Disjoint detail CPU work. Legacy index_ms = bounds + build + refit;
    // legacy select_ms = sun + local. Both may be nested in preflight/plan.
    double shadow_bounds_ms=0,shadow_bvh_build_ms=0,shadow_bvh_refit_ms=0;
    double shadow_sun_select_ms=0,shadow_local_select_ms=0;
    enum RebuildReason { SourceChanged=1, CountChanged=2, OriginOrderChanged=4,
        KnownBoundsChanged=8, EmptyOrInvalid=16, NoRetainedTopology=32 };
    uint32_t shadow_rebuild_reasons=0;
    ShadowBounds BoundsFor(const kich_r4_detail::Mesh& mesh,const KichR4DetailInstance& instance,uint32_t variant)const{
        ShadowBounds result;if(!mesh.bounds_known || !source || variant>2)return result;
        for(unsigned corner=0;corner<8;++corner){const float p[3]={corner&1?mesh.maximum[0]:mesh.minimum[0],corner&2?mesh.maximum[1]:mesh.minimum[1],corner&4?mesh.maximum[2]:mesh.minimum[2]};
            for(unsigned a=0;a<3;++a){const float value=instance.row[a][0]*p[0]+instance.row[a][1]*p[1]+instance.row[a][2]*p[2]+instance.row[a][3];
                if(!kich_r4_detail::Finite(value))return result;
                if(!corner || value<result.low[a])result.low[a]=value;
                if(!corner || value>result.high[a])result.high[a]=value;}}
        if(variant){const float height=std::max(std::fabs(result.low[1]-instance.row[1][3]),std::fabs(result.high[1]-instance.row[1][3]));
            for(unsigned a=0;a<3;a+=2){const float pad=std::fabs(source->wind[variant][a])*height*mesh.height_factor;result.low[a]-=pad;result.high[a]+=pad;}}
        // Invalid authoring/wind data must never poison a merged node and
        // make another instance disappear. Preserve it as an unbounded leaf.
        for(unsigned a=0;a<3;++a)if(!kich_r4_detail::Finite(result.low[a]) || !kich_r4_detail::Finite(result.high[a]) || result.low[a]>result.high[a])return result;
        result.known=true;return result;
    }
    bool ShadowVisible(const kich_r4_detail::Mesh& mesh,const KichR4DetailInstance& instance,uint32_t variant,const kich_r4_scene::Matrix& vp)const{
        const ShadowBounds bounds=BoundsFor(mesh,instance,variant);return !bounds.known || kich_r4_visibility::Visible(bounds.low,bounds.high,vp);
    }
    static void IncludeShadowBounds(ShadowBounds& target,const ShadowBounds& other){
        for(unsigned a=0;a<3;++a){target.low[a]=(std::min)(target.low[a],other.low[a]);target.high[a]=(std::max)(target.high[a],other.high[a]);}
    }
    uint32_t BuildShadowNode(uint32_t first,uint32_t count){
        const uint32_t id=static_cast<uint32_t>(shadow_nodes.size());ShadowNode node={};
        node.first=first;node.count=count;node.left=node.right=UINT32_MAX;node.bounds=shadow_bounds[shadow_order[first]].bounds;
        for(uint32_t i=1;i<count;++i)IncludeShadowBounds(node.bounds,shadow_bounds[shadow_order[first+i]].bounds);
        shadow_nodes.push_back(node);
        if(count>8){unsigned axis=0;for(unsigned a=1;a<3;++a)if(double(node.bounds.high[a])-node.bounds.low[a]>double(node.bounds.high[axis])-node.bounds.low[axis])axis=a;
            const uint32_t middle=first+count/2;
            std::nth_element(shadow_order.begin()+first,shadow_order.begin()+middle,shadow_order.begin()+first+count,[&](uint32_t a,uint32_t b){
                const ShadowBounds& x=shadow_bounds[a].bounds;const ShadowBounds& y=shadow_bounds[b].bounds;
                const double cx=double(x.low[axis])+x.high[axis],cy=double(y.low[axis])+y.high[axis];return cx==cy?a<b:cx<cy;});
            const uint32_t left=BuildShadowNode(first,count/2),right=BuildShadowNode(middle,count-count/2);
            shadow_nodes[id].left=left;shadow_nodes[id].right=right;
        }
        return id;
    }
    bool ShadowNodeVisible(const ShadowNode& node,const kich_r4_scene::Matrix& vp,bool sun)const{
        // A small expansion is used only for coarse rejection. Exact leaves
        // retain the original tests, including their clip-plane tolerance.
        ShadowBounds bounds=node.bounds;float magnitude=1;
        for(unsigned a=0;a<3;++a)magnitude=(std::max)(magnitude,(std::max)(std::fabs(bounds.low[a]),std::fabs(bounds.high[a])));
        const float pad=.001f+magnitude*.00001f;
        for(unsigned a=0;a<3;++a){bounds.low[a]-=pad;bounds.high[a]+=pad;}
        return kich_r4_visibility::Visible(bounds.low,bounds.high,vp) && (!sun || sun_casters.Visible(bounds.low,bounds.high));
    }
    void VisitShadowNode(uint32_t id,const kich_r4_scene::Matrix& vp,bool sun){
        const ShadowNode& node=shadow_nodes[id];++shadow_node_tests;if(!ShadowNodeVisible(node,vp,sun))return;
        if(node.left!=UINT32_MAX){VisitShadowNode(node.left,vp,sun);VisitShadowNode(node.right,vp,sun);return;}
        for(uint32_t j=0;j<node.count;++j){const uint32_t index=shadow_order[node.first+j];const ShadowBounds& bounds=shadow_bounds[index].bounds;++shadow_visibility_tests;
            if(kich_r4_visibility::Visible(bounds.low,bounds.high,vp) && (!sun || sun_casters.Visible(bounds.low,bounds.high)))shadow_hits.push_back(index);}
    }
    void PrepareShadowSpatial(){
        if(shadow_spatial_ready)return;
        const auto started=kich_r4_metrics::Start(g_render_snapshot_active);
        const bool rebuilding=!shadow_topology_valid;
        if(rebuilding){++shadow_index_builds;shadow_order.clear();shadow_unknown.clear();shadow_nodes.clear();shadow_order.reserve(shadow_bounds.size());
            for(size_t i=0;i<shadow_bounds.size();++i)(shadow_bounds[i].bounds.known?shadow_order:shadow_unknown).push_back(static_cast<uint32_t>(i));
            if(!shadow_order.empty())BuildShadowNode(0,static_cast<uint32_t>(shadow_order.size()));
            shadow_topology_valid=true;
        }else{++shadow_index_refits;
            // Fresh transformed/wind bounds are mandatory even if the same
            // source origins allow us to retain the spatial partition.
            for(size_t at=shadow_nodes.size();at>0;--at){ShadowNode& node=shadow_nodes[at-1];
                if(node.left!=UINT32_MAX){node.bounds=shadow_nodes[node.left].bounds;IncludeShadowBounds(node.bounds,shadow_nodes[node.right].bounds);}
                else{node.bounds=shadow_bounds[shadow_order[node.first]].bounds;
                    for(uint32_t i=1;i<node.count;++i)IncludeShadowBounds(node.bounds,shadow_bounds[shadow_order[node.first+i]].bounds);}}
        }
        shadow_spatial_ready=true;const double elapsed=kich_r4_metrics::Elapsed(started);shadow_index_ms+=elapsed;
        if(rebuilding)shadow_bvh_build_ms+=elapsed;else shadow_bvh_refit_ms+=elapsed;
    }
    void BeginVisibilityFrame(uint32_t frame,const kich_r4_visibility::SunCasterVolume& volume){
        const auto started=kich_r4_metrics::Start(g_render_snapshot_active);
        const bool probing=started.measured;
        shadow_rebuild_reasons=probing && !shadow_topology_valid?NoRetainedTopology:0;
        if(visibility_source!=source || visibility_generation!=(source?source->generation:UINT64_MAX)){
            shadow_topology_valid=false;if(probing)shadow_rebuild_reasons|=SourceChanged;}
        visibility_frame=frame;visibility_source=source;visibility_generation=source?source->generation:UINT64_MAX;sun_casters=volume;
        shadow_views.clear();shadow_hits.clear();shadow_spatial_ready=false;
        shadow_bounds_built=shadow_visibility_tests=shadow_cache_hits=shadow_cached_bytes=shadow_source_candidates=shadow_node_tests=shadow_queries=0;
        shadow_index_ms=shadow_select_ms=0;shadow_index_builds=shadow_index_refits=0;visibility_valid=true;
        shadow_bounds_ms=shadow_bvh_build_ms=shadow_bvh_refit_ms=shadow_sun_select_ms=shadow_local_select_ms=0;
        if(source && source->source_frame_known && source->engine_frame==frame){
            for(size_t i=0;i<source->shadow_groups.size();++i){const kich_r4_detail::Group& g=source->shadow_groups[i];shadow_source_candidates+=g.count;
                if(g.mesh>=source->meshes.size() || g.variant>2 || g.first>source->shadow_instances.size() || g.count>source->shadow_instances.size()-g.first)visibility_valid=false;}
            // Index fields address compact source ranges; reject overflow just
            // as Draw rejects malformed groups, never truncate casters.
            if(shadow_source_candidates>UINT32_MAX || source->shadow_groups.size()>UINT32_MAX)visibility_valid=false;
            if(visibility_valid){
                if(shadow_bounds.size()!=shadow_source_candidates){shadow_topology_valid=false;if(probing)shadow_rebuild_reasons|=CountChanged;}
                shadow_bounds.resize(static_cast<size_t>(shadow_source_candidates));size_t at=0;
                for(size_t i=0;i<source->shadow_groups.size();++i){const kich_r4_detail::Group& g=source->shadow_groups[i];
                    for(uint32_t j=0;j<g.count;++j,++at){const auto& instance=source->shadow_instances[g.first+j];
                        const ShadowCandidate candidate={BoundsFor(source->meshes[g.mesh],instance,g.variant),g.first+j,static_cast<uint32_t>(i),{instance.row[0][3],instance.row[1][3],instance.row[2][3]}};
                        const ShadowCandidate& previous=shadow_bounds[at];
                        const bool known_changed=previous.bounds.known!=candidate.bounds.known;
                        const bool origin_changed=memcmp(previous.origin,candidate.origin,sizeof(candidate.origin))!=0;
                        if(known_changed || origin_changed){shadow_topology_valid=false;
                            if(probing)shadow_rebuild_reasons|=(known_changed?KnownBoundsChanged:0)|(origin_changed?OriginOrderChanged:0);}
                        shadow_bounds[at]=candidate;++shadow_bounds_built;}}
            }
        }else shadow_bounds.clear();
        if(!visibility_valid)shadow_bounds.clear();
        if(shadow_bounds.empty()){shadow_topology_valid=false;if(probing)shadow_rebuild_reasons|=EmptyOrInvalid;}
        if(!shadow_topology_valid){shadow_nodes.clear();shadow_order.clear();shadow_unknown.clear();}
        shadow_bounds_ms=kich_r4_metrics::Elapsed(started);shadow_index_ms=shadow_bounds_ms;
    }
    void EnsureVisibilityFrame(uint32_t frame,const kich_r4_visibility::SunCasterVolume& volume){
        if(visibility_frame!=frame || visibility_source!=source || visibility_generation!=(source?source->generation:UINT64_MAX))BeginVisibilityFrame(frame,volume);
    }
    void AppendShadowHit(ShadowSelection& selected,uint32_t index,uint32_t& previous)const{
        const ShadowCandidate& candidate=shadow_bounds[index];
        if(candidate.group!=previous){const auto& source_group=source->shadow_groups[candidate.group];
            const kich_r4_detail::Group group={source_group.mesh,source_group.variant,static_cast<uint32_t>(selected.instances.size()),0};selected.groups.push_back(group);previous=candidate.group;}
        selected.instances.push_back(candidate.instance);++selected.groups.back().count;
    }
    const ShadowSelection& SelectShadow(const kich_r4_scene::Matrix& vp,bool sun){
        ++shadow_queries;ShadowViewKey key;memcpy(key.data(),vp.m,64);key[16]=sun?1u:0u;
        const auto found=shadow_views.find(key);if(found!=shadow_views.end()){++shadow_cache_hits;return found->second;}
        const bool current=source && source->source_frame_known && source->engine_frame==visibility_frame;
        // Two ordinary sun views and tiny lists are cheaper as linear scans.
        // Build/refit at most once only when additional shadow views need it.
        // This is a work-selection heuristic, never a draw/caster quota.
        const bool spatial=current && visibility_valid && shadow_bounds.size()>64 && shadow_queries-shadow_cache_hits>2;
        if(spatial)PrepareShadowSpatial();
        // Index time is separate from selection time, including a lazy build.
        const auto started=kich_r4_metrics::Start(g_render_snapshot_active);ShadowSelection selected;selected.valid=visibility_valid;
        if(current){selected.candidates=shadow_source_candidates;
            if(selected.valid){uint32_t previous=UINT32_MAX;
                if(!spatial){
                    for(size_t i=0;i<shadow_bounds.size();++i){const ShadowBounds& bounds=shadow_bounds[i].bounds;++shadow_visibility_tests;
                        if(!bounds.known || (kich_r4_visibility::Visible(bounds.low,bounds.high,vp) && (!sun || sun_casters.Visible(bounds.low,bounds.high))))AppendShadowHit(selected,static_cast<uint32_t>(i),previous);}
                }else{
                    shadow_hits=shadow_unknown;shadow_visibility_tests+=shadow_unknown.size();if(!shadow_nodes.empty())VisitShadowNode(0,vp,sun);
                    selected.instances.reserve(shadow_hits.size());
                    if(shadow_hits.size()>shadow_bounds.size()/8){
                        // Wide cascades may retain almost everything. A linear
                        // mark/scan avoids sorting all accepted leaf ordinals.
                        shadow_hit_marks.assign(shadow_bounds.size(),0);for(size_t i=0;i<shadow_hits.size();++i)shadow_hit_marks[shadow_hits[i]]=1;
                        for(size_t i=0;i<shadow_hit_marks.size();++i)if(shadow_hit_marks[i])AppendShadowHit(selected,static_cast<uint32_t>(i),previous);
                    }else{std::sort(shadow_hits.begin(),shadow_hits.end());for(size_t i=0;i<shadow_hits.size();++i)AppendShadowHit(selected,shadow_hits[i],previous);}
                }
            }
        }
        const double elapsed=kich_r4_metrics::Elapsed(started);shadow_select_ms+=elapsed;
        if(sun)shadow_sun_select_ms+=elapsed;else shadow_local_select_ms+=elapsed;
        const uint64_t bytes=sizeof(ShadowSelection)+sizeof(key)+selected.instances.size()*sizeof(uint32_t)+selected.groups.size()*sizeof(kich_r4_detail::Group);
        if(bytes<=16u*1024u*1024u && shadow_cached_bytes<=16u*1024u*1024u-bytes){shadow_cached_bytes+=bytes;return shadow_views.emplace(key,std::move(selected)).first->second;}
        uncached_shadow=std::move(selected);return uncached_shadow;
    }
    int32_t UploadShadow(KichR4FrameSession& s,KichR4LevelSession& l){
        if(selected_instances.empty())return 0;
        if(selected_instances.size()>KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3b detail shadow instance budget",KICH_R4_E_MEMORY);
        const uint32_t needed=static_cast<uint32_t>(selected_instances.size());
        if(needed>shadow_capacity){uint32_t next=256;while(next<needed)next*=2;KichR4Handle handle=0;
            if(l.Expect(s,"G3b shadow instance stream",api.create_instances(s.device,next,&handle))<0)return s.failure;
            if(shadow_instance_buffer && l.Expect(s,"G3b replace shadow instance stream",l.bridge.resources.release_resource(s.device,shadow_instance_buffer))<0)return s.failure;
            shadow_instance_buffer=handle;shadow_capacity=next;}
        if(l.Expect(s,"G3b shadow instance WRITE_DISCARD",api.upload_instances(s.device,shadow_instance_buffer,selected_instances.data(),needed))<0)return s.failure;
        shadow_upload_bytes+=uint64_t(needed)*sizeof(KichR4DetailInstance);return 0;
    }
    int32_t Upload(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t frame){
        ResetFrame(frame);
        if(!source)return 0;
        if(generation!=source->generation){
            for(size_t i=0;i<meshes.size();++i){
                if(meshes[i].vb && l.Expect(s,"G3a release old mesh VB",l.bridge.resources.release_resource(s.device,meshes[i].vb))<0)return s.failure;
                if(meshes[i].ib && l.Expect(s,"G3a release old mesh IB",l.bridge.resources.release_resource(s.device,meshes[i].ib))<0)return s.failure;}
            meshes.clear();vertex_bytes=index_bytes=0;uploaded_frame=UINT32_MAX;
            for(size_t i=0;i<source->meshes.size();++i){const kich_r4_detail::Mesh& m=source->meshes[i];Mesh gpu;
                if(m.vertices.empty() || m.indices.empty() || m.vertices.size()>65536 || m.indices.size()>UINT32_MAX/2)return s.Record("G3a invalid detail mesh",KICH_R4_E_ARGUMENT);
                vertex_bytes+=m.vertices.size()*sizeof(m.vertices[0]);index_bytes+=m.indices.size()*2;
                if(vertex_bytes+index_bytes>64u*1024u*1024u)return s.Record("G3a detail mesh budget",KICH_R4_E_MEMORY);
                const KichR4BufferDesc vb={sizeof(vb),KICH_R4_VERTEX_BUFFER,static_cast<uint32_t>(m.vertices.size()*sizeof(m.vertices[0])),sizeof(m.vertices[0])};
                const KichR4BufferDesc ib={sizeof(ib),KICH_R4_INDEX_BUFFER,static_cast<uint32_t>(m.indices.size()*2),2};
                if(l.Expect(s,"G3a immutable mesh vertices",l.bridge.resources.create_buffer(s.device,&vb,m.vertices.data(),vb.byte_size,&gpu.vb))<0)return s.failure;
                if(l.Expect(s,"G3a immutable mesh indices",l.bridge.resources.create_buffer(s.device,&ib,m.indices.data(),ib.byte_size,&gpu.ib))<0)return s.failure;
                gpu.indices=static_cast<uint32_t>(m.indices.size());meshes.push_back(gpu);
            }
            generation=source->generation;
        }
        if(source->engine_frame!=frame || source->instances.empty())return 0;
        if(source->instances.size()>KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3a instance budget",KICH_R4_E_MEMORY);
        const uint32_t needed=static_cast<uint32_t>(source->instances.size());
        if(needed>capacity){uint32_t next=256;while(next<needed)next*=2;KichR4Handle handle=0;
            if(l.Expect(s,"G3a dynamic instance stream",api.create_instances(s.device,next,&handle))<0)return s.failure;
            if(instance_buffer && l.Expect(s,"G3a replace instance stream",l.bridge.resources.release_resource(s.device,instance_buffer))<0)return s.failure;
            instance_buffer=handle;capacity=next;
        }
        if(l.Expect(s,"G3a instance WRITE_DISCARD",api.upload_instances(s.device,instance_buffer,source->instances.data(),needed))<0)return s.failure;
        uploaded_frame=frame;uploaded_instances=needed;return 0;
    }
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& l,const char* hlsl,KichR4Handle geometry_ps,KichR4Handle shadow_ps){
        if(!api.create_pipeline || !api.create_instances || !api.upload_instances || !api.draw)return s.Record("G3a detail API",KICH_R4_E_STATE);
        if(vs && pipelines[0] && pipelines[1])return 0;
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=KICH_R4_VERTEX_SHADER;memcpy(d.entry,"VSDetail",9);memcpy(d.name,"kich_g3a_details",16);
        if(l.Expect(s,"G3a VSDetail",l.bridge.resources.compile_shader(s.device,&d,hlsl,static_cast<uint32_t>(strlen(hlsl)),&vs,l.bridge.shader_error,sizeof(l.bridge.shader_error)))<0)return s.failure;
        for(unsigned i=0;i<2;++i){const KichR4DetailPipelineDesc p={sizeof(p),vs,i?shadow_ps:geometry_ps};
            if(l.Expect(s,"G3a instance input layout",api.create_pipeline(s.device,&p,&pipelines[i]))<0)return s.failure;}
        return 0;
    }
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle cb,const kich_r4_lighting::Constants& base,bool shadow,uint32_t frame,const char* phase="unspecified",uint32_t pass_mask=0){
        struct Summary {char section[64];uint32_t calls,allocated_capacity;uint64_t variants[3],candidates,culled,selected,uploaded_bytes;bool enabled,shadow;
            Summary(const char* name,bool is_shadow):calls(0),allocated_capacity(0),candidates(0),culled(0),selected(0),uploaded_bytes(0),enabled(g_render_snapshot_active),shadow(is_shadow){section[0]=0;if(enabled)snprintf(section,sizeof(section),"DETAIL_PASS %s",name);variants[0]=variants[1]=variants[2]=0;}
            ~Summary(){if(enabled){RenderSnapshot_U64(section,"draw_calls",calls);RenderSnapshot_U64(section,"submitted_instances",variants[0]+variants[1]+variants[2]);
                RenderSnapshot_Text(section,"candidate_scope",shadow?"READY_CACHE_WITHIN_DETAIL_RADIUS":"CAMERA_SELECTED_LIST");RenderSnapshot_U64(section,"instance_capacity",allocated_capacity);RenderSnapshot_U64(section,"instance_allocated_bytes",uint64_t(allocated_capacity)*sizeof(KichR4DetailInstance));
                RenderSnapshot_U64(section,"candidate_instances",candidates);RenderSnapshot_U64(section,"culled_instances",culled);RenderSnapshot_U64(section,"selected_instances",selected);RenderSnapshot_U64(section,"instance_upload_bytes",uploaded_bytes);
                RenderSnapshot_U64(section,"still_submitted_instances",variants[0]);RenderSnapshot_U64(section,"wave1_submitted_instances",variants[1]);RenderSnapshot_U64(section,"wave2_submitted_instances",variants[2]);}}
        } summary(phase,shadow);
        summary.allocated_capacity=shadow?shadow_capacity:capacity;
        if(!source || source->engine_frame!=frame || generation!=source->generation || telemetry_frame!=frame)return 0;
        kich_r4_shadow_record::Pass* record=shadow?kich_r4_shadow_record::Active():NULL;
        kich_r4_shadow_record::Work detail_selection;
        const std::vector<kich_r4_detail::Group>* groups=&source->groups;
        if(shadow){selected_instances.clear();selected_groups.clear();
            EnsureVisibilityFrame(frame,sun_casters);
            const ShadowSelection& selected=SelectShadow(base.mvp,strncmp(phase,"sun_",4)==0);
            if(!selected.valid)return s.Record("G3b detail shadow group range",KICH_R4_E_ARGUMENT);
            if(selected.instances.size()>KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3b selected detail budget",KICH_R4_E_MEMORY);
            selected_groups=selected.groups;selected_instances.reserve(selected.instances.size());
            size_t record_group=0;
            for(size_t i=0;i<selected.instances.size();++i){
                const KichR4DetailInstance& instance=source->shadow_instances[selected.instances[i]];
                selected_instances.push_back(instance);
                if(record && record->signatures){
                    while(record_group<selected.groups.size() && i>=uint64_t(selected.groups[record_group].first)+selected.groups[record_group].count)++record_group;
                    if(record_group<selected.groups.size() && selected.groups[record_group].mesh<meshes.size()){
                        const kich_r4_detail::Group& group=selected.groups[record_group];
                        detail_selection.Caster(kich_r4_shadow_record::DetailID(group.mesh,instance.row[0][3],instance.row[1][3],instance.row[2][3]),group.variant,meshes[group.mesh].indices);
                    }else detail_selection.Caster(0,0,0,false);
                }
            }
            if(record && !record->signatures)detail_selection.casters=detail_selection.unknown_ids=selected.instances.size();
            summary.candidates=selected.candidates;summary.selected=selected.instances.size();summary.culled=selected.candidates-summary.selected;
            shadow_candidates+=summary.candidates;shadow_culled+=summary.culled;
            if(UploadShadow(s,l)<0)return s.failure;
            summary.allocated_capacity=shadow_capacity;summary.uploaded_bytes=uint64_t(selected_instances.size())*sizeof(KichR4DetailInstance);groups=&selected_groups;
        }else{if(uploaded_frame!=frame || source->instances.empty())return 0;summary.candidates=summary.selected=source->instances.size();summary.uploaded_bytes=uint64_t(uploaded_instances)*sizeof(KichR4DetailInstance);}
        for(size_t i=0;i<groups->size();++i){const kich_r4_detail::Group& g=(*groups)[i];
            const size_t count=shadow?selected_instances.size():source->instances.size();
            if(g.mesh>=meshes.size() || g.variant>2 || g.first>count || g.count>count-g.first)return s.Record("G3a detail group range",KICH_R4_E_ARGUMENT);
            const Mesh& mesh=meshes[g.mesh];kich_r4_lighting::Constants c=base;
            if(RenderSnapshot_Detailed()){try{const kich_r4_detail::Mesh& named=source->meshes[g.mesh];
                const std::string material=MaterialKey(named.shader+"\n"+named.texture+"\ncutout=200/255;two_sided;depth_write");
                RenderSnapshot_Context("DETAIL",named.name.c_str(),named.shader.c_str());RenderSnapshot_Object("DETAIL",named.name.c_str(),material.c_str(),"LEVEL_DETAILS_INSTANCE_GROUP",0);
                RenderSnapshot_Queue("R4_DETAIL_INSTANCED");RenderSnapshot_PassMask(pass_mask);RenderSnapshot_DetailVariant(g.variant);
                float uv[8]={};uint32_t samples=0;for(size_t j=0;j<named.indices.size() && samples<4;++j){const uint16_t index=named.indices[j];if(index<named.vertices.size()){uv[samples*2]=named.vertices[index].misc[0];uv[samples*2+1]=named.vertices[index].misc[1];++samples;}}
                RenderSnapshot_UV(uv,samples,"DETAIL_INDEXED_MESH_QUANTIZED_BASE_UV");RenderSnapshot_Texture(0,named.texture.c_str());RenderSnapshot_Skin("NONE_INSTANCED_DETAIL");
                const std::string normal_id="detail/"+named.name;
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"geometry_normal_source","VSDetail: NORMALIZE(WORLD_POSITION - INSTANCE_BASE_MINUS_0.75_Y)");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"normal_texture_variant","NOT_SUPPORTED_BY_VSDetail_PSGeometry");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"tangent_basis","NOT_PRESENT_IN_DETAIL_LAYOUT");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"normal_buffer_value","NOT_READ_BACK; PSGeometry NORMALIZATION_AND_FACE_SIGN");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"uv_measurement","FIRST_FOUR_INDEXED_SAMPLES; NOT_FULL_RANGE");
                char wave[192];snprintf(wave,sizeof(wave),"%.9g,%.9g,%.9g,%.9g",source->wave[g.variant][0],source->wave[g.variant][1],source->wave[g.variant][2],source->wave[g.variant][3]);
                RenderSnapshot_Record("DETAIL_GEOMETRY",normal_id.c_str(),"wave_constants",wave);
                snprintf(wave,sizeof(wave),"%.9g,%.9g,%.9g,%.9g",source->wind[g.variant][0],source->wind[g.variant][1],source->wind[g.variant][2],source->wind[g.variant][3]);
                RenderSnapshot_Record("DETAIL_GEOMETRY",normal_id.c_str(),"wind_constants",wave);
                RenderSnapshot_RecordU64("DETAIL_GEOMETRY",normal_id.c_str(),"variant",g.variant);
                }catch(...){RenderSnapshot_Context("DETAIL","NOT_AVAILABLE","NOT_AVAILABLE");RenderSnapshot_DetailVariant(g.variant);RenderSnapshot_Text("TELEMETRY_LOSS","r4_detail_provenance","ALLOCATION_FAILED");}}
            /* VSDetail owns world rows: wave, wind, variant. Other shaders keep
             * their ordinary matrix meaning; the 816-byte frame ABI is intact. */
            memcpy(c.world.m,source->wave[g.variant],16);memcpy(c.world.m+4,source->wind[g.variant],16);
            c.world.m[8]=static_cast<float>(g.variant);c.alpha[0]=200.f/255;c.alpha[1]=7;c.alpha[2]=c.alpha[3]=0;
            if(l.Expect(s,"G3a detail constants",l.bridge.api.update_buffer(s.device,cb,&c,sizeof(c)))<0)return s.failure;
            KichR4DetailDrawDesc d={};d.struct_size=sizeof(d);d.surface.struct_size=sizeof(d.surface);
            KichR4StreamDrawDesc& state=d.surface.state;state.struct_size=sizeof(state);
            state.draw={sizeof(state.draw),pipelines[shadow?1:0],mesh.vb,mesh.ib,cb,mesh.indices,0,KICH_R4_CULL_NONE,KICH_R4_DEPTH_ON};
            state.source_blend=2;state.dest_blend=1;state.depth_write=1;state.max_depth=1;
            d.surface.textures[0]=l.Texture(s,source->meshes[g.mesh].texture);d.instance_buffer=shadow?shadow_instance_buffer:instance_buffer;d.first_instance=g.first;d.instance_count=g.count;
            if(l.Expect(s,shadow?"G3a detail shadow DrawIndexedInstanced":"G3a detail G-buffer DrawIndexedInstanced",api.draw(s.device,&d))<0)return s.failure;
            if(record)record->work[kich_r4_shadow_record::Detail].Draw(mesh.indices,g.count);
            ++summary.calls;summary.variants[g.variant]+=g.count;
            if(shadow){++shadow_draw_calls;shadow_submitted_instances+=g.count;}else{++draw_calls;submitted_instances+=g.count;}
        }
        if(record)record->work[kich_r4_shadow_record::Detail].Add(detail_selection);
        return 0;
    }
};
#endif
