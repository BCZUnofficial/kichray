#ifndef KICH_R4_DEFERRED_SESSION_H
#define KICH_R4_DEFERRED_SESSION_H
#include "KichR4Deferred.h"
#include "KichR4DeferredShaders.h"
#include "KichR4StreamSession.h"
#include "KichR4LightingMath.h"
#include "KichR4LightProjector.h"
#include "KichR4Game.h"
#include "KichR4NativeTools.h"
#include "KichR4DetailSession.h"
#include "KichR4ModelSession.h"
#include "KichR4Visibility.h"
#include "KichR4LevelVisibility.h"
#include "KichR4MaterialSession.h"
#include "KichR4TerrainMaterial.h"
#include "KichR4OpticalRules.h"
#include "KichR4LodPolicy.h"
#include "KichR4PostprocessNoise.h"
#include "KichR4ShadowPoolPolicy.h"
#include "KichR4LocalShadowPlan.h"
#include "KichR4ShadowRecord.h"
#include "KichR4Bloom.h"
#include <set>
#include <map>
/* EXE-side pass planner. Native resources remain owned by the R4 device;
 * destroying that device releases partial construction and all cached maps. */
struct KichR4DeferredSession {
    enum Pipeline {StaticGeometry,ModelGeometry,StaticShadow,ModelShadow,Sun,Local,ForwardSun,ForwardLocal,Plain,Combine,Sky,Clouds,Water,Distortion,StaticForward,StaticWater,Copy,Postprocess,StaticPlain,DistortionMask,DistortionComposite,CelestialPipeline,WallmarkPipeline,StaticWallmarkPipeline,
        BloomBuild,BloomFilter,BloomFilterFast,BloomComposite,Luminance1,Luminance2,Luminance3,SunShafts,SunShaftsComposite,HudBloomSource,SunShaftsBlur,PipelineCount};
    KichR4DeferredAPI api;
    enum {ShaderCount=32};
    KichR4Handle pipelines[PipelineCount],shaders[ShaderCount],cb,screen_vb,screen_ib,white;
    // P0-C2: R2 generic1 (high = colour/8, RGBA8) and the 256^2 bloom1/bloom2.
    KichR4Handle high_color=0,bloom1=0,bloom2=0;
    // P0-C1: R2 lum_t64/lum_t8 (RGBA16F) and the 1x1 R32F exposure pool. [0] is
    // last frame's result (s_tonemap source), [1] is read by sky/combine/clouds
    // and receives this frame's result; they swap at the end of the frame.
    KichR4Handle lum64=0,lum8=0,lum_pool[2]={0,0};bool lum_ready=false;float luminance_adapt=.5f;
    KichR4Handle albedo,normal,depth,receiver_depth,light,sun_map,sun_far_map,scene_color,final_color,distortion_map;
    KichR4Handle sun_mask=0; // this frame's t6 sunmask or white, shared by sun-lit forward draws
    uint32_t width,height,frames,shadow_passes,local_passes;
    // Successful submissions in this frame, not a measurement of visible shadow pixels.
    uint32_t frame_sun_cascades=0,frame_spot_shadow_lights=0,frame_point_shadow_lights=0;
    uint32_t frame_local_shadow_faces=0;
    // Count each selected spot once in opaque local accumulation, never HUD/forward repeats.
    uint32_t frame_projector_spots=0,frame_projector_named=0,frame_projector_ready=0,frame_projector_fallback=0;
    kich_r4_shadow_record::Frame shadow_record;
    uint64_t sun_map_generation[3]={0,0,0},local_pool_generation=0;
    KichR4Handle sun_contact_map=0; // P0-S2 contact cascade, bound at t7 when shadow[2] is valid
    int32_t sun_shadow_cull=0; // this frame's KichRay r2_sun_shadow_cull
    uint32_t sun_map_size=2048; // P0-S5 resolution of the allocated contact/128 m sun maps
    uint32_t sun_far_map_size=2048; // P0-S7 resolution of the allocated 512 m sun map
    KichR4Handle shafts=0,shafts_blur=0; // P0-S5 full-size RGBA16F volume light and its blur ping-pong
    // One bounded pool, shared by deferred, forward and HUD lighting. A point
    // source keeps all six faces; pressure reduces resolution, never drops it.
    std::vector<KichR4Handle> local_maps;
    std::vector<uint32_t> local_offsets;
    uint32_t local_shadow_size=512;
    static const uint64_t LocalShadowBudget=kich_r4_shadow_pool::BudgetBytes;
    // P0-S7 planned frames (KichR4LocalShadowPlan.h): each light has its own
    // size and renders only its visible faces; a culled face binds a 16x16 map
    // at depth 1 (lit). Maps are pooled per size and reused across frames.
    bool local_planned=false;
    std::vector<KichR4Handle> light_face_maps; // lighting.count*6
    std::vector<uint32_t> light_sizes;
    std::map<uint32_t,std::vector<KichR4Handle>> local_size_pool;
    KichR4Handle lit_face_map=0;bool lit_face_ready=false;
    uint32_t current_local_size=512; // the light whose faces are being rendered (caster LOD)
    KichR4Handle LocalMap(unsigned index,unsigned face)const {
        if(local_planned)return size_t(index)*6+face<light_face_maps.size()?light_face_maps[size_t(index)*6+face]:0;
        return index<local_offsets.size() && local_offsets[index]!=UINT32_MAX?
            local_maps[local_offsets[index]+face]:0;
    }
    uint32_t LocalSize(unsigned index)const{return local_planned && index<light_sizes.size() && light_sizes[index]?light_sizes[index]:local_shadow_size;}
    bool LocalFaceRendered(unsigned index,unsigned face)const{const KichR4Handle map=LocalMap(index,face);return map && (!local_planned || map!=lit_face_map);}
    int32_t PrepareLocalPlan(KichR4FrameSession& s,KichR4LevelSession& l,const KichR4LightingFrame& lighting,const std::vector<uint8_t>& selected){
        // Entering the plan retires the uniform legacy pool.
        for(size_t i=0;i<local_maps.size();++i)if(l.Expect(s,"P0-S7 retire uniform shadow pool",l.bridge.resources.release_resource(s.device,local_maps[i]))<0)return s.failure;
        local_maps.clear();local_offsets.clear();
        local_planned=true;light_face_maps.assign(size_t(lighting.count)*6,0);light_sizes.assign(lighting.count,0);
        if(!lit_face_map){if(Target(s,l,lit_face_map,16,16,KICH_R4_DEPTH32F,true)<0)return s.failure;lit_face_ready=false;}
        if(!lit_face_ready){if(Pass(s,l,16,16,0,0,0,lit_face_map,KICH_R4_CLEAR_DEPTH)<0)return s.failure;lit_face_ready=true;}
        std::map<uint32_t,size_t> used;uint64_t payload=0;uint32_t faces=0,largest=0;
        for(unsigned i=0;i<lighting.count;++i){if(!selected[i])continue;
            const uint32_t mask=lighting.ShadowFaceMask(i),size=lighting.ShadowSize(i);
            if(size<kich_r4_shadow_pool::MinimumSize || size>kich_r4_local_shadow::MaximumClass || (size&(size-1)))return s.Record("P0-S7 local shadow size",KICH_R4_E_ARGUMENT);
            light_sizes[i]=size;largest=(std::max)(largest,size);
            for(unsigned face=0;face<(lighting.lights[i].type==2?1u:6u);++face){
                if(!(mask&(1u<<face))){light_face_maps[size_t(i)*6+face]=lit_face_map;continue;}
                std::vector<KichR4Handle>& bucket=local_size_pool[size];size_t& n=used[size];
                if(n==bucket.size()){KichR4Handle map=0;if(Target(s,l,map,size,size,KICH_R4_DEPTH32F,true)<0)return s.failure;bucket.push_back(map);++local_pool_generation;}
                light_face_maps[size_t(i)*6+face]=bucket[n++];payload+=uint64_t(size)*size*4;++faces;}}
        if(payload>LocalShadowBudget)return s.Record("P0-S7 local shadow plan budget",KICH_R4_E_ARGUMENT);
        // Idle maps stay for reuse up to twice the budget; beyond that they go.
        uint64_t allocated=0;for(const auto& b:local_size_pool)allocated+=uint64_t(b.first)*b.first*4*b.second.size();
        if(allocated>2*LocalShadowBudget)for(auto& b:local_size_pool){const auto found=used.find(b.first);const size_t keep=found!=used.end()?found->second:0;
            while(b.second.size()>keep){if(l.Expect(s,"P0-S7 local shadow pool trim",l.bridge.resources.release_resource(s.device,b.second.back()))<0)return s.failure;b.second.pop_back();allocated-=uint64_t(b.first)*b.first*4;}}
        if(largest)local_shadow_size=largest;
        if(g_render_snapshot_active){RenderSnapshot_U64("SHADOW_POOL","requested_faces",faces);RenderSnapshot_U64("SHADOW_POOL","resolution",largest);
            RenderSnapshot_U64("SHADOW_POOL","budget_bytes",LocalShadowBudget);RenderSnapshot_U64("SHADOW_POOL","planned_bytes",payload);RenderSnapshot_U64("SHADOW_POOL","allocated_bytes",allocated);
            RenderSnapshot_Text("SHADOW_POOL","policy","P0-S7 R2 PER-LIGHT SIZE (POW2 64..1024, HYSTERESIS); VISIBLE FACES ONLY (CAMERA FRUSTUM, HUD BOX, HOM); BUDGET HALVES LARGEST");}
        return 0;
    }
    int32_t PrepareLocalMaps(KichR4FrameSession& s,KichR4LevelSession& l,const KichR4LightingFrame& lighting,const std::vector<uint8_t>& selected){
        if(lighting.ShadowPlanned())return PrepareLocalPlan(s,l,lighting,selected);
        if(local_planned){local_planned=false;light_face_maps.clear();light_sizes.clear();}
        local_offsets.assign(lighting.count,UINT32_MAX);uint32_t faces=0;
        for(unsigned i=0;i<lighting.count;++i)if(selected[i]){local_offsets[i]=faces;faces+=lighting.lights[i].type==2?1:6;}
        // Gameplay freezes resolution with the source list before collecting
        // casters. Legacy direct producers have no plan and use a bounded fit.
        const uint32_t size=lighting.local_shadow_size?lighting.local_shadow_size:kich_r4_shadow_pool::BestFit(faces);
        if(!kich_r4_shadow_pool::Fits(faces,size) ||
            (lighting.local_shadow_size && lighting.shadow_plan_faces!=faces))
            return s.Record("E1.5 local shadow plan/budget",KICH_R4_E_ARGUMENT);
        if(size!=local_shadow_size){
            for(size_t i=0;i<local_maps.size();++i)if(l.Expect(s,"E1 shadow pool resize",l.bridge.resources.release_resource(s.device,local_maps[i]))<0)return s.failure;
            local_maps.clear();local_shadow_size=size;++local_pool_generation;
        }
        while(local_maps.size()<faces){KichR4Handle map=0;if(Target(s,l,map,size,size,KICH_R4_DEPTH32F,true)<0)return s.failure;local_maps.push_back(map);++local_pool_generation;}
        if(g_render_snapshot_active){RenderSnapshot_U64("SHADOW_POOL","requested_faces",faces);RenderSnapshot_U64("SHADOW_POOL","allocated_faces",local_maps.size());
            RenderSnapshot_U64("SHADOW_POOL","resolution",size);RenderSnapshot_U64("SHADOW_POOL","budget_bytes",LocalShadowBudget);
            RenderSnapshot_U64("SHADOW_POOL","allocated_bytes",uint64_t(local_maps.size())*size*size*4);
            RenderSnapshot_Text("SHADOW_POOL","policy",lighting.local_shadow_size?"FROZEN_FRAME_RESOLUTION; IMMEDIATE_PRESSURE; 2000MS_RECOVERY_WITH_25_PERCENT_HEADROOM":"DIRECT_FRAME_BOUNDED_FIT");}
        return 0;
    }
    kich_r4_lighting::Constants base;
    bool native_mode;KichR4NativeToolsAPI tools;
    std::set<uint32_t> simple_transparent;
    KichR4DetailSession details;
    KichR4ModelSession native_models;
    kich_r4_level::StaticFramePlan visibility_plan;
    uint32_t visibility_frame=UINT32_MAX,visibility_revision=UINT32_MAX;
    uint32_t static_candidates[2]={0,0};
    std::vector<kich_r4_scene::Matrix> static_normals;
    std::vector<float> static_lods;
    uint32_t normals_revision=UINT32_MAX;
    const kich_r4_level::Draw* normals_storage=NULL;
    float lod_start=256.f,lod_end=64.f;
    std::vector<size_t> plain_order,mixed_order,static_wallmark_order;
    std::vector<kich_r4_visibility::Bounds> stream_bounds;
    const kich_r4_capture::Batch* bounded_batch=NULL;
    const kich_r4_capture::Batch* ordered_batch=NULL;
    float ordered_camera[3]={};
    uint32_t forward_sorts=0;
    bool mixed_order_ready=false;
    bool StaticEligible(const KichR4LevelSession& l,const kich_r4_level::Draw& mesh,bool shadow)const{
        const kich_r4_level::Material& m=l.scene.materials[mesh.material];
        const bool described=native_mode && m.descriptor.semantic!=kich_r4_material::UNKNOWN;
        return !native_mode || (described?(shadow?kich_r4_material::Shadow(m.descriptor):kich_r4_material::Geometry(m.descriptor)):m.kind<2);
    }
    void BeginStaticPlan(KichR4LevelSession& l,float x,float z){
        const kich_r4_level::Draw* storage=l.scene.draws.empty()?NULL:&l.scene.draws[0];
        if(normals_revision!=l.revision || normals_storage!=storage || static_normals.size()!=l.scene.draws.size()){
            static_normals.resize(l.scene.draws.size());for(size_t i=0;i<l.scene.draws.size();++i)static_normals[i]=kich_r4_lighting::NormalMatrix(l.scene.draws[i].world);
            normals_revision=l.revision;normals_storage=storage;}
        visibility_plan.Begin(l.scene,l.revision,x,z);static_candidates[0]=static_candidates[1]=0;
        for(size_t i=0;i<l.scene.draws.size();++i){static_candidates[0]+=StaticEligible(l,l.scene.draws[i],false)?1:0;static_candidates[1]+=StaticEligible(l,l.scene.draws[i],true)?1:0;}
    }
    void BeginFramePlan(KichR4LevelSession& l,const KichR4GameFrame& input){
        BeginStaticPlan(l,input.tree_wind[0],input.tree_wind[2]);visibility_frame=input.engine_frame;visibility_revision=l.revision;
        kich_r4_scene::Matrix camera;memcpy(camera.m,input.view_projection,sizeof(camera.m));
        visibility_plan.sun_casters=kich_r4_visibility::BuildSunCasterVolume(camera,input.lighting.sun_direction);
        native_models.SetSunCasterVolume(visibility_plan.sun_casters);
        details.BeginVisibilityFrame(input.engine_frame,visibility_plan.sun_casters);
        const kich_r4_lod::Parameters lod=kich_r4_lod::CameraParameters(input.width,input.height,input.geometry_lod[3],input.geometry_lod[0],input.geometry_lod[1],input.geometry_lod[2]);
        lod_start=input.geometry_lod[1];lod_end=input.geometry_lod[2];static_lods.resize(l.scene.draws.size());
        for(size_t i=0;i<static_lods.size();++i)static_lods[i]=kich_r4_lod::Geometry(&visibility_plan.bounds[i].low.x,&visibility_plan.bounds[i].high.x,input.position,lod);
        ordered_batch=bounded_batch=NULL;mixed_order_ready=false;forward_sorts=0;
    }
    void EnsureStaticPlan(KichR4LevelSession& l){
        if(visibility_plan.scene!=&l.scene || visibility_plan.revision!=l.revision || visibility_plan.bounds.size()!=l.scene.draws.size())
            BeginStaticPlan(l,base.tree_wind[0],base.tree_wind[2]);
    }
    uint32_t snapshot_pass_mask=0;
    char snapshot_phase[64]={};
    bool ordinary_profile=false;
    bool profile_context_valid=true;
    void Mark(KichR4FrameSession& s,unsigned n){if(native_mode && profile_context_valid && (ordinary_profile || g_render_snapshot_active) && tools.profile_mark)tools.profile_mark(s.device,n);}
    void SnapshotPass(const char* name,uint32_t mask,const char* phase="main"){
        snapshot_pass_mask=mask;SnapshotPhase(phase);
        if(!g_render_snapshot_active)return;
        RenderSnapshot_Pass(name);RenderSnapshot_Phase(phase);
    }
    void SnapshotPhase(const char* phase){snprintf(snapshot_phase,sizeof(snapshot_phase),"%s",phase?phase:"main");if(g_render_snapshot_active)RenderSnapshot_Phase(snapshot_phase);}
    static void SnapshotLocalLight(const KichR4LocalLight& light_source,const KichR4LightIdentity& source,unsigned index,bool shadowed){
        if(!g_render_snapshot_active)return;
        char id[48],section[64],value[160];
        if(source.serial)snprintf(id,sizeof(id),"source_%llu",static_cast<unsigned long long>(source.serial));else snprintf(id,sizeof(id),"local_%u",index);
        snprintf(section,sizeof(section),"LIGHT %s",id);const char* scope=source.serial?"PROCESS_LIFETIME_SOURCE_SERIAL":"FRAME_LOCAL_LIGHT_ARRAY_INDEX";
        RenderSnapshot_Light(id,scope);RenderSnapshot_Text(section,"id_scope",scope);
        RenderSnapshot_U64(section,"frame_array_index",index);
        snprintf(value,sizeof(value),"%llu",static_cast<unsigned long long>(source.serial));RenderSnapshot_Text(section,"source_serial",value);
        snprintf(value,sizeof(value),"0x%llX",static_cast<unsigned long long>(source.process_handle));RenderSnapshot_Text(section,"source_handle_process_local",value);
        RenderSnapshot_U64(section,"static_source",(source.flags&KICH_R4_LIGHT_STATIC)!=0);
        RenderSnapshot_Text(section,"role",source.role==KICH_R4_LIGHT_ROLE_LEVEL?"LEVEL_RECORD":source.role==KICH_R4_LIGHT_ROLE_RUNTIME?"RUNTIME_REGISTERED":"UNKNOWN");
        const bool level_valid=source.role==KICH_R4_LIGHT_ROLE_LEVEL && source.level_record!=UINT32_MAX;
        const bool controller_valid=source.role==KICH_R4_LIGHT_ROLE_LEVEL && source.controller!=UINT32_MAX;
        RenderSnapshot_U64(section,"level_record_valid",level_valid);RenderSnapshot_U64(section,"controller_valid",controller_valid);
        if(level_valid)RenderSnapshot_U64(section,"level_record",source.level_record);else RenderSnapshot_Text(section,"level_record","NOT_AVAILABLE");
        if(controller_valid)RenderSnapshot_U64(section,"controller",source.controller);else RenderSnapshot_Text(section,"controller","NOT_AVAILABLE");RenderSnapshot_Text(section,"type",light_source.type==2?"SPOT":"POINT");
        snprintf(value,sizeof(value),"%.9g,%.9g,%.9g",light_source.position[0],light_source.position[1],light_source.position[2]);RenderSnapshot_Text(section,"position",value);
        snprintf(value,sizeof(value),"%.9g,%.9g,%.9g",light_source.direction[0],light_source.direction[1],light_source.direction[2]);RenderSnapshot_Text(section,"direction",value);
        snprintf(value,sizeof(value),"%.9g,%.9g,%.9g",light_source.color[0],light_source.color[1],light_source.color[2]);RenderSnapshot_Text(section,"color",value);
        RenderSnapshot_Value(section,"range",light_source.range);RenderSnapshot_Value(section,"cone",light_source.cone);RenderSnapshot_U64(section,"shadow_selected",shadowed?1:0);
        kich_r4_telemetry::U64("LIGHT",id,"selected",1);kich_r4_telemetry::U64("LIGHT",id,"shadow_selected",shadowed?1:0);
        kich_r4_telemetry::Vector("LIGHT",id,"input_color",light_source.color,3);kich_r4_telemetry::Vector("LIGHT",id,"applied_color",light_source.color,3);
        kich_r4_telemetry::Text("LIGHT",id,"color_transform","NONE; COPIED_TO_LIGHT_COLOR_RGB");
        const kich_r4_scene::Vec3 normalized=kich_r4_scene::Normalize(kich_r4_scene::Vec3{light_source.direction[0],light_source.direction[1],light_source.direction[2]});const float direction[]={normalized.x,normalized.y,normalized.z};
        kich_r4_telemetry::Vector("LIGHT",id,"applied_normalized_direction",direction,3);kich_r4_telemetry::Float("LIGHT",id,"applied_cone_cos_half",cosf(light_source.cone*.5f));
        kich_r4_telemetry::Text("LIGHT",id,"attenuation","POINT=saturate(1-distanceSquared/range^2); SPOT=saturate(1-distanceSquared/(range*.95)^2), PROJECTED_RGBA_WITH_CONE_BOUNDARY_OR_ANALYTIC_FALLBACK");
        kich_r4_telemetry::Text("LIGHT",id,"pixel_contribution","NOT_MEASURED");kich_r4_telemetry::Text("LIGHT",id,"affected_objects","NOT_MEASURED; FULLSCREEN_LIGHT_USES_DEPTH_NORMAL; FORWARD_TESTS_REPORTED_BY_PHASE");
    }
    static void SnapshotCPU(const kich_r4_capture::Batch* batch){
        if(!g_render_snapshot_active)return;
        using namespace kich_r4_capture;using namespace kich_r4_telemetry;
        uint64_t commands[CAPTURE_SEMANTIC_COUNT]={},vertices[CAPTURE_SEMANTIC_COUNT]={},indices[CAPTURE_SEMANTIC_COUNT]={};
        uint64_t attributed=0;
        if(batch)for(size_t i=0;i<batch->commands.size();++i){const Command& c=batch->commands[i];const unsigned semantic=c.semantic<CAPTURE_SEMANTIC_COUNT?c.semantic:unsigned(CAPTURE_UNKNOWN);
            ++commands[semantic];vertices[semantic]+=c.captured_vertices;indices[semantic]+=c.count;attributed+=c.captured_vertices;}
        for(unsigned i=0;i<CAPTURE_SEMANTIC_COUNT;++i){const char* id=CaptureSemanticName(i);U64("CPU_CAPTURE",id,"commands",commands[i]);U64("CPU_CAPTURE",id,"vertices",vertices[i]);U64("CPU_CAPTURE",id,"indices",indices[i]);
            U64("CPU_CAPTURE",id,"vertex_bytes",vertices[i]*sizeof(Vertex));U64("CPU_CAPTURE",id,"index_bytes",indices[i]*sizeof(uint32_t));}
        const uint64_t total=batch?batch->vertices.size():0;
        RenderSnapshot_U64("CPU_CAPTURE","total_commands",batch?batch->commands.size():0);RenderSnapshot_U64("CPU_CAPTURE","total_vertices",total);
        RenderSnapshot_U64("CPU_CAPTURE","total_indices",batch?batch->indices.size():0);RenderSnapshot_U64("CPU_CAPTURE","vertex_bytes",total*sizeof(Vertex));RenderSnapshot_U64("CPU_CAPTURE","index_bytes",batch?batch->indices.size()*sizeof(uint32_t):0);
        RenderSnapshot_U64("CPU_CAPTURE","unattributed_vertices",total>attributed?total-attributed:0);
        RenderSnapshot_U64("CPU_CAPTURE","semantic_vertex_accounting_valid",attributed==total?1:0);
        RenderSnapshot_Text("CPU_CAPTURE","scope","COMPLETE_CAPTURE_COMMAND_ARRAY_BEFORE_TRACE_LIMIT; RESIDUAL_STREAM_ONLY; NATIVE_MODELS_EXCLUDED");
        RenderSnapshot_Text("CPU_CAPTURE","semantic_index_bytes_scope","SUM_OF_REFERENCED_COMMAND_RANGES; MAY_REPEAT_SHARED_RANGES; TOTAL_INDEX_BYTES_IS_ACTUAL_BUFFER_PAYLOAD");
    }
    static void SnapshotShadow(const char* id,const char* parent,unsigned face,const kich_r4_scene::Matrix& matrix,KichR4Handle target,bool sun,float range,unsigned resolution=512,float span=512){
        if(!g_render_snapshot_active)return;
        using namespace kich_r4_telemetry;
        Text("SHADOW",id,"source_light_id",parent);U64("SHADOW",id,"face_or_cascade",face);Text("SHADOW",id,"unit",sun?"STABILIZED_NEAR_FAR_SUN_MAP": "ONE_LIGHT_FACE; NOT_ONE_LAMP");
        Text("SHADOW",id,"format","DEPTH32F/D32_FLOAT");U64("SHADOW",id,"width",resolution);U64("SHADOW",id,"height",resolution);U64("SHADOW",id,"target_handle",target);
        Float("SHADOW",id,"near",sun?0.f:kich_r4_lighting::LocalShadowNear(range));Float("SHADOW",id,"far",sun?1024.f:range);Vector("SHADOW",id,"view_projection_row_major",matrix.m,16);
        Float("SHADOW",id,"orthographic_span",sun?span:0);Text("SHADOW",id,"projection",sun?"TEXEL_SNAPPED_CAMERA_CENTERED":"PERSPECTIVE_SPOT_CONE_OR_POINT_90_DEG_PLUS_2_TEXEL_GUARD");
        Float("SHADOW",id,"comparison_depth_bias",0);Float("SHADOW",id,"receiver_bias_meters",sun?.01f:.003f);Text("SHADOW",id,"comparison_bias_source","WORLD_SPACE_RECEIVER_TOWARD_LIGHT; CAMERA_RELATIVE_QUAD_RECEIVER_PLANE; GEOMETRIC_NORMAL_PRECISION_ALLOWANCE_MAX_0.002_M; NO_FIXED_NONLINEAR_DEPTH_SUBTRACTION");
        Text("SHADOW",id,"rasterizer_bias","DEFAULT_ZERO; NO_PER_LIGHT_OVERRIDE");Text("SHADOW",id,"filter","VALID_PLANE_3x3_POINT_COMPARISON_TEXEL_CENTRE_QUADRATIC_TENT; INVALID_PLANE_SINGLE_POINT; LESS_EQUAL");
        Text("SHADOW",id,"candidate_source","LEVEL_STATIC+DETAILS+MODEL_CAMERA_SHADOW_UNION; PASS_FRUSTUM_CULL");Text("SHADOW",id,"stencil_volumes","NOT_SUPPORTED");
        Text("SHADOW",id,"execution","PLANNED");
    }
    static void SnapshotSubmittedLight(const KichR4LightIdentity& source,unsigned index){
        if(!g_render_snapshot_active)return;
        char id[64];if(source.serial)kich_r4_telemetry::Handle(id,sizeof(id),"source",source.serial);else snprintf(id,sizeof(id),"local_%u",index);
        kich_r4_telemetry::U64("LIGHT",id,"submitted",1);kich_r4_telemetry::Text("LIGHT",id,"submission_status","LOCAL_ACCUMULATION_DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED");
    }
    void SnapshotStatic(const KichR4LevelSession& l,size_t index,const char* queue,const kich_r4_level::Draw* selected=NULL){
        try{
        const kich_r4_level::Draw& mesh=selected?*selected:l.scene.draws[index];const kich_r4_level::Material& m=l.scene.materials[mesh.material];
        char id[160],state[192];snprintf(id,sizeof(id),"level_scene/draw_%u/vb_%u/ib_%u/first_%u/count_%u",static_cast<unsigned>(index),mesh.vb,mesh.ib,mesh.first,mesh.count);
        snprintf(state,sizeof(state),"kind=%u;blend=%u/%u/%u;alpha=%u/%.9g;cutout=%u",m.kind,m.blend,m.source,m.destination,m.alpha_func,m.alpha_ref,m.cutout?1:0);
        char descriptor_key[32];snprintf(descriptor_key,sizeof(descriptor_key),"%016llx",static_cast<unsigned long long>(kich_r4_material::Key(m.descriptor)));
        std::string identity=m.shader+"\n"+m.texture+"\n"+m.texture_list+"\n"+state+"\n"+descriptor_key;for(unsigned i=0;i<4;++i)identity+="\n"+m.layers[i];
        const std::string material=KichR4DetailSession::MaterialKey(identity);const char* semantic=m.kind==2?"WATER":"STATIC";
        RenderSnapshot_Context(semantic,id,m.shader.c_str());RenderSnapshot_Object(semantic,id,material.c_str(),"LEVEL_SCENE_DRAW_INDEX_AND_RANGES",0);
        RenderSnapshot_Queue(queue);RenderSnapshot_PassMask(snapshot_pass_mask);RenderSnapshot_Skin("NONE");
        char route[64];snprintf(route,sizeof(route),"NATIVE_MATERIAL:%016llx",static_cast<unsigned long long>(kich_r4_material::Key(m.descriptor)));
        RenderSnapshot_MaterialRoute(route);kich_r4_material::SnapshotMetadata(m.descriptor,"NATIVE_MATERIAL",route);
        const bool resolved=native_mode && m.descriptor.semantic!=kich_r4_material::UNKNOWN;
        const float applied_ref=resolved?m.descriptor.alpha_ref:native_mode?m.alpha_ref:m.cutout?200.f/255.f:0.f;
        const uint32_t applied_func=resolved?m.descriptor.alpha_func:native_mode?m.alpha_func:applied_ref>0?7u:8u;
        kich_r4_telemetry::U64("NATIVE_MATERIAL",route,"effective_alpha_func",applied_func);
        kich_r4_telemetry::Float("NATIVE_MATERIAL",route,"effective_alpha_ref",applied_ref);
        kich_r4_telemetry::Text("NATIVE_MATERIAL",route,"route_source",resolved?"RESOLVED_DESCRIPTOR":"LEGACY_STATIC_FALLBACK");
        RenderSnapshot_Texture(0,m.texture.c_str());for(unsigned i=1;i<8;++i)RenderSnapshot_Texture(i,"HANDLE_ONLY");
        if(m.kind==1){RenderSnapshot_Texture(2,(m.texture+"_mask").c_str());for(unsigned i=0;i<4;++i)RenderSnapshot_Texture(i+3,m.layers[i].c_str());}
        float uv[8]={};uint32_t samples=0;
        if(mesh.vb<l.scene.vertices.size() && mesh.ib<l.scene.indices.size()){const std::vector<uint32_t>& indices=l.scene.indices[mesh.ib];const std::vector<kich_r4_level::Vertex>& vertices=l.scene.vertices[mesh.vb].vertices;
            for(uint32_t i=0;i<mesh.count && samples<4 && uint64_t(mesh.first)+i<indices.size();++i){const uint32_t v=indices[mesh.first+i];if(v<vertices.size()){uv[samples*2]=vertices[v].uv[0];uv[samples*2+1]=vertices[v].uv[1];++samples;}}}
        RenderSnapshot_UV(uv,samples,"LEVEL_INDEXED_BASE_UV");
        }catch(...){RenderSnapshot_Context("STATIC","NOT_AVAILABLE","NOT_AVAILABLE");RenderSnapshot_Queue(queue);RenderSnapshot_PassMask(snapshot_pass_mask);RenderSnapshot_Text("TELEMETRY_LOSS","r4_static_provenance","ALLOCATION_FAILED");}
    }
    static void SnapshotResolvedTexture(const KichR4LevelSession& level,unsigned stage,const char* source,KichR4Handle handle){
        if(!g_render_snapshot_active)return;
        if(handle==level.checker && handle){char label[160];snprintf(label,sizeof(label),"CHECKER_FALLBACK:%s",source?source:"");RenderSnapshot_Texture(stage,label);}
        else if(handle==level.neutral_cube && handle)RenderSnapshot_Texture(stage,"NEUTRAL_CUBE_FALLBACK");
        else RenderSnapshot_Texture(stage,source && *source?source:"$white");
    }
    static void SnapshotVisibility(const char* status,const char* reason){
        RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);RenderSnapshot_Note(status,p.semantic_type[0]?p.semantic_type:"UNKNOWN",p.mesh_id[0]?p.mesh_id:"NOT_AVAILABLE",reason);
    }
    KichR4DeferredSession():cb(0),screen_vb(0),screen_ib(0),white(0),albedo(0),normal(0),depth(0),receiver_depth(0),light(0),sun_map(0),sun_far_map(0),scene_color(0),final_color(0),distortion_map(0),width(0),height(0),frames(0),shadow_passes(0),local_passes(0),native_mode(false){
        memset(&tools,0,sizeof(tools));
        memset(&api,0,sizeof(api));memset(pipelines,0,sizeof(pipelines));memset(shaders,0,sizeof(shaders));memset(&base,0,sizeof(base));}
    static bool ValidAPI(const KichR4DeferredAPI& a){return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_DEFERRED_ABI &&
        a.pointer_size==sizeof(void*) && !a.flags && a.create_pipeline && a.begin_pass && a.draw;}
    // P0-S7: the five lighting shaders (PSSun, PSLocal, PSForwardSun/Local,
    // PSWater) take most of the compile time and no menu frame needs them. They
    // compile with the first scene frame, under the level loading screen.
    static bool SceneShader(unsigned i){return i==6 || i==7 || i==8 || i==9 || i==14;}
    bool scene_shaders_ready=false;
    int32_t StartScene(KichR4FrameSession& s,KichR4LevelSession& level){
        if(scene_shaders_ready)return 0;
        if(CompileShaders(s,level,true)<0)return s.failure;
        scene_shaders_ready=true;return 0;
    }
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& level){
        if(!ValidAPI(api))return s.Record("B4.4 deferred ABI",KICH_R4_E_ARGUMENT);
        if(CompileShaders(s,level,false)<0)return s.failure;
        return StartResources(s,level);
    }
    // Compiles the missing shaders (without scene: all but SceneShader) and
    // creates every pipeline whose two shaders exist.
    int32_t CompileShaders(KichR4FrameSession& s,KichR4LevelSession& level,bool scene){
        const char* entries[]={"VSStatic","VSModel","VSModelShadow","VSScreen","PSGeometry","PSShadow","PSSun","PSLocal","PSForwardSun","PSForwardLocal","PSPlain","PSCombine","PSSky","PSClouds","PSWater","PSDistortion","PSPostprocess","PSParticleDistortionMask","PSDistortionComposite","PSCelestial","PSWallmark",
            "PSBloomBuild","PSBloomFilter","PSBloomFilterFast","PSBloomComposite","PSLuminance1","PSLuminance2","PSLuminance3","PSSunShafts","PSSunShaftsComposite","PSHudBloomSource","PSSunShaftsBlur"};
        static_assert(sizeof(entries)/sizeof(entries[0])==ShaderCount,"deferred shader entries");
        const char* source=KichR4DeferredShaderSource();
        for(unsigned i=0;i<ShaderCount;++i){if(shaders[i] || (!scene && SceneShader(i)))continue;
            level.Continue();KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=i<4?KICH_R4_VERTEX_SHADER:KICH_R4_PIXEL_SHADER;
            memcpy(d.entry,entries[i],strlen(entries[i])+1);memcpy(d.name,"kich_b44_deferred",18);
            if(level.Expect(s,entries[i],level.bridge.resources.compile_shader(s.device,&d,source,static_cast<uint32_t>(strlen(source)),&shaders[i],level.bridge.shader_error,sizeof(level.bridge.shader_error)))<0)return s.failure;}
        const unsigned vs[]={0,1,0,2,3,3,1,1,1,3,1,1,1,1,0,0,3,3,0,1,3,1,1,0,3,3,3,3,3,3,3,3,3,3,3},ps[]={4,4,5,5,6,7,8,9,10,11,12,13,14,15,8,14,10,16,10,17,18,19,20,20,21,22,23,24,25,26,27,28,29,30,31};
        static_assert(sizeof(vs)/sizeof(vs[0])==PipelineCount && sizeof(ps)/sizeof(ps[0])==PipelineCount,"deferred pipeline table");
        for(unsigned i=0;i<PipelineCount;++i){if(pipelines[i] || !shaders[vs[i]] || !shaders[ps[i]])continue;
            KichR4PipelineDesc d={};d.struct_size=sizeof(d);d.vertex_shader=shaders[vs[i]];d.pixel_shader=shaders[ps[i]];
            const bool model=vs[i]==1 || vs[i]==2,screen=vs[i]==3;
            d.vertex_stride=model?sizeof(kich_r4_capture::Vertex):screen?20:sizeof(kich_r4_level::Vertex);
            d.element_count=model?5:screen?2:8;
            d.elements[0]={KICH_R4_POSITION,0,static_cast<uint32_t>(model?KICH_R4_FLOAT4:KICH_R4_FLOAT3),0};
            d.elements[1]={KICH_R4_TEXCOORD,0,KICH_R4_FLOAT2,model?16u:12u};
            if(model){d.elements[2]={KICH_R4_COLOR,0,KICH_R4_UNORM8X4,24};d.elements[3]={KICH_R4_NORMAL,0,KICH_R4_FLOAT4,28};d.elements[4]={KICH_R4_TEXCOORD,1,KICH_R4_FLOAT3,44};}
            else if(!screen){d.elements[2]={KICH_R4_NORMAL,0,KICH_R4_FLOAT4,20};
                d.elements[3]={KICH_R4_TEXCOORD,1,KICH_R4_FLOAT3,36};d.elements[4]={KICH_R4_TEXCOORD,2,KICH_R4_FLOAT3,48};
                d.elements[5]={KICH_R4_TEXCOORD,3,KICH_R4_FLOAT3,60};d.elements[6]={KICH_R4_COLOR,0,KICH_R4_UNORM8X4,72};d.elements[7]={KICH_R4_TEXCOORD,4,KICH_R4_FLOAT2,76};}
            if(level.Expect(s,"B4.4 deferred pipeline",api.create_pipeline(s.device,&d,&pipelines[i]))<0)return s.failure;}
        return 0;
    }
    int32_t StartResources(KichR4FrameSession& s,KichR4LevelSession& level){
        KichR4BufferDesc b={sizeof(b),KICH_R4_CONSTANT_BUFFER,sizeof(base),0};
        if(level.Expect(s,"B4.4 constants",level.bridge.resources.create_buffer(s.device,&b,&base,sizeof(base),&cb))<0)return s.failure;
        const float vertices[15]={-1,-1,0,0,1,-1,3,0,0,-1,3,-1,0,2,1};const uint32_t indices[3]={0,1,2};
        b.kind=KICH_R4_VERTEX_BUFFER;b.stride=20;b.byte_size=sizeof(vertices);
        if(level.Expect(s,"B4.4 screen vertices",level.bridge.resources.create_buffer(s.device,&b,vertices,sizeof(vertices),&screen_vb))<0)return s.failure;
        b.kind=KICH_R4_INDEX_BUFFER;b.stride=4;b.byte_size=sizeof(indices);
        if(level.Expect(s,"B4.4 screen indices",level.bridge.resources.create_buffer(s.device,&b,indices,sizeof(indices),&screen_ib))<0)return s.failure;
        std::vector<unsigned char> d(132,0);KichR4LevelSession::Put32(d,0,0x20534444);KichR4LevelSession::Put32(d,4,124);
        KichR4LevelSession::Put32(d,8,0x100f);KichR4LevelSession::Put32(d,12,1);KichR4LevelSession::Put32(d,16,1);KichR4LevelSession::Put32(d,20,4);
        KichR4LevelSession::Put32(d,76,32);KichR4LevelSession::Put32(d,80,0x41);KichR4LevelSession::Put32(d,88,32);
        KichR4LevelSession::Put32(d,92,0xff);KichR4LevelSession::Put32(d,96,0xff00);KichR4LevelSession::Put32(d,100,0xff0000);KichR4LevelSession::Put32(d,104,0xff000000);KichR4LevelSession::Put32(d,108,0x1000);
        for(unsigned i=128;i<132;++i)d[i]=255;
        const KichR4DdsDesc dd={sizeof(dd),2048,0};
        return level.Expect(s,"B4.4 stream white",level.api.create_texture_dds(s.device,&dd,d.data(),static_cast<uint32_t>(d.size()),&white));
    }
    // P0-S5: the two full-size RGBA16F shaft targets (~33 MB at 1080p) exist
    // only while shafts are on; switching r4_sun_shafts to 0 releases them.
    int32_t ShaftTargets(KichR4FrameSession& s,KichR4LevelSession& l,bool enabled){
        if(enabled){if(!shafts && Target(s,l,shafts,width,height,KICH_R4_RGBA16F,false)<0)return s.failure;
            if(!shafts_blur && Target(s,l,shafts_blur,width,height,KICH_R4_RGBA16F,false)<0)return s.failure;return 0;}
        KichR4Handle* targets[2]={&shafts,&shafts_blur};
        for(unsigned i=0;i<2;++i)if(*targets[i]){if(l.Expect(s,"P0-S5 release shaft target",l.bridge.resources.release_resource(s.device,*targets[i]))<0)return s.failure;*targets[i]=0;}
        return 0;
    }
    int32_t Target(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle& h,uint32_t w,uint32_t n,uint32_t format,bool z){
        const KichR4TextureDesc d={sizeof(d),w,n,format,static_cast<uint32_t>(KICH_R4_SHADER_READ|(z?KICH_R4_DEPTH_TARGET:KICH_R4_COLOR_TARGET))};
        return l.Expect(s,"B4.4 target allocation",l.bridge.resources.create_texture(s.device,&d,&h));}
    int32_t ScreenTargets(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t w,uint32_t h){
        if(w==width && h==height)return 0;
        // Native: 48 bytes of G-buffer/colour targets plus 16 of P0-S5 shafts per pixel; same resolution limit.
        if(uint64_t(w)*h*(native_mode?64u:28u)>(native_mode?512u:192u)*1024u*1024u)return s.Record("B4.4 screen target budget",KICH_R4_E_MEMORY);
        KichR4Handle* old[]={&albedo,&normal,&depth,&receiver_depth,&light,&scene_color,&final_color,&distortion_map,&high_color,&bloom1,&bloom2,&lum64,&lum8,&lum_pool[0],&lum_pool[1],&shafts,&shafts_blur};
        lum_ready=false;
        for(unsigned i=0;i<sizeof(old)/sizeof(old[0]);++i)if(*old[i]){if(l.Expect(s,"B4.4 old screen target",l.bridge.resources.release_resource(s.device,*old[i]))<0)return s.failure;*old[i]=0;}
        if(Target(s,l,albedo,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,normal,w,h,KICH_R4_RGBA16F,false)<0 ||
            Target(s,l,depth,w,h,KICH_R4_DEPTH24_STENCIL8,true)<0 || Target(s,l,receiver_depth,w,h,KICH_R4_R32F,false)<0 ||
            Target(s,l,light,w,h,KICH_R4_RGBA16F,false)<0)return s.failure;
        if(native_mode && (Target(s,l,scene_color,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,final_color,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,distortion_map,w,h,KICH_R4_RGBA16F,false)<0))return s.failure;
        // R2 creates generic1 and bloom1/bloom2 as A8R8G8B8; the 8-bit clamp is part of the look.
        if(native_mode && (Target(s,l,high_color,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,bloom1,kich_r4_bloom::Size,kich_r4_bloom::Size,KICH_R4_RGBA8,false)<0 ||
            Target(s,l,bloom2,kich_r4_bloom::Size,kich_r4_bloom::Size,KICH_R4_RGBA8,false)<0 ||
            Target(s,l,lum64,64,64,KICH_R4_RGBA16F,false)<0 || Target(s,l,lum8,8,8,KICH_R4_RGBA16F,false)<0 ||
            Target(s,l,lum_pool[0],1,1,KICH_R4_R32F,false)<0 || Target(s,l,lum_pool[1],1,1,KICH_R4_R32F,false)<0))return s.failure;
        // P0-S5 shafts targets are created on first use (ShaftTargets), never while r4_sun_shafts is 0.
        width=w;height=h;return 0;
    }
    int32_t Pass(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t w,uint32_t h,uint32_t count,KichR4Handle c0,KichR4Handle c1,KichR4Handle z,uint32_t clear,const float* color=NULL,KichR4Handle c2=0,KichR4Handle c3=0){
        if(g_render_snapshot_active){for(unsigned i=0;i<8;++i)RenderSnapshot_Texture(i,"HANDLE_ONLY");}
        KichR4PassDesc p={};p.struct_size=sizeof(p);p.color_count=count;p.colors[0]=c0;p.colors[1]=c1;p.colors[2]=c2;p.colors[3]=c3;p.depth=z;p.width=w;p.height=h;p.clear_flags=clear;p.depth_value=1;if(color)memcpy(p.color,color,sizeof(p.color));
        return l.Expect(s,"B4.4 begin pass",api.begin_pass(s.device,&p));}
    int32_t Constants(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_lighting::Constants& c){
        return l.Expect(s,"B4.4 upload constants",l.bridge.api.update_buffer(s.device,cb,&c,sizeof(c)));}
    static KichR4DeferredDrawDesc Description(KichR4Handle pipeline,KichR4Handle vb,KichR4Handle ib,KichR4Handle constants,uint32_t count,uint32_t first,const kich_r4_capture::Style& style){
        KichR4DeferredDrawDesc d={};d.struct_size=sizeof(d);d.state.struct_size=sizeof(d.state);
        d.state.draw={sizeof(d.state.draw),pipeline,vb,ib,constants,count,first,style.cull,style.depth};
        d.state.blend_enable=style.blend;d.state.source_blend=style.src;d.state.dest_blend=style.dst;d.state.clamp_texture=style.clamp;
        d.state.depth_write=style.write;d.state.scissor_enable=style.scissor?1:0;memcpy(d.state.scissor,style.rect,sizeof(d.state.scissor));
        d.state.min_depth=style.min_z;d.state.max_depth=style.max_z;d.state.surface=style.surface;return d;
    }
    static bool Opaque(const kich_r4_capture::Command& c){return c.layer==0 && !c.style.blend && c.style.depth && c.style.write && (!c.effect || c.style.emissive_gain>0);}
    int32_t Static(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_scene::Matrix& vp,bool shadow,uint32_t sector=UINT32_MAX){
        if(!shadow)l.visible=l.culled=0;
        if(!shadow && l.files.sector)sector=l.files.sector(base.camera);
        EnsureStaticPlan(l);
        const kich_r4_level::StaticVisibility& visibility=visibility_plan.Select(vp,sector,shadow && strncmp(snapshot_phase,"sun_",4)==0);
        if(g_render_snapshot_active){char id[96];snprintf(id,sizeof(id),"STATIC_VISIBILITY %s",snapshot_phase);
            RenderSnapshot_U64(id,"portal_used",visibility.portal_used);RenderSnapshot_U64(id,"sectors",visibility.sectors);
            RenderSnapshot_U64(id,"portal_tests",visibility.portal_tests);RenderSnapshot_U64(id,"sector_rejected",visibility.sector_rejected);
            RenderSnapshot_U64(id,"frustum_rejected",visibility.frustum_rejected);
            RenderSnapshot_U64(id,"bounds_tests",visibility.bounds_tests);RenderSnapshot_U64(id,"bvh_node_tests",visibility.node_tests);
            uint64_t selected=0;for(size_t n=0;n<visibility.selected.size();++n)selected+=StaticEligible(l,l.scene.draws[visibility.selected[n]],shadow)?1:0;
            const uint64_t omitted=static_candidates[shadow?1:0]>=selected?static_candidates[shadow?1:0]-selected:0;
            RenderSnapshot_VisibilityTotals(omitted,omitted,0);}
        if(!shadow)l.culled=static_candidates[0];
        for(size_t n=0;n<visibility.selected.size();++n){const size_t i=visibility.selected[n];kich_r4_level::Draw mesh=l.scene.draws[i];
            const kich_r4_level::Material& m=l.scene.materials[mesh.material];const kich_r4_material::Descriptor& material=m.descriptor;
            const bool described=native_mode && material.semantic!=kich_r4_material::UNKNOWN;
            if(native_mode && (described?!(shadow?kich_r4_material::Shadow(material):kich_r4_material::Geometry(material)):m.kind>=2))continue;
            if(mesh.lod){float lod=i<static_lods.size()?static_lods[i]:1.f;
                if(shadow){const uint32_t resolution=strncmp(snapshot_phase,"sun_",4)==0?sun_map_size:current_local_size;
                    const kich_r4_level::Draw& bounds=visibility_plan.bounds[i];lod=(std::max)(lod,kich_r4_lod::Projected(&bounds.low.x,&bounds.high.x,vp,resolution,resolution,lod_start,lod_end));}
                const kich_r4_level::DrawRange range=kich_r4_level::SelectDrawRange(l.scene,mesh,lod);mesh.first=range.first;mesh.count=range.count;mesh.available_attributes=range.available_attributes;}
            if(g_render_snapshot_active){SnapshotStatic(l,i,"R4_STATIC_OPAQUE",&mesh);SnapshotVisibility("CANDIDATE_CLASSIFIED",shadow?"SHADOW_FRUSTUM":"CAMERA_FRUSTUM");}
            if(!shadow && l.culled)--l.culled;
            if(g_render_snapshot_active)SnapshotVisibility("SELECTED",shadow?"SHADOW_FRUSTUM":"CAMERA_FRUSTUM");
            kich_r4_lighting::Constants c=base;c.world=mesh.world;c.mvp=vp;
            c.normal_matrix=static_normals[i];
            const float cutoff=native_mode?m.alpha_ref:m.cutout?200.f/255:0;c.alpha[0]=cutoff;c.alpha[1]=native_mode?float(m.alpha_func):cutoff>0?7.f:8.f;
            kich_r4_capture::Style style;style.cull=l.cull;style.depth=style.write=1;
            if(described)kich_r4_material::Apply(material,style);
            if(shadow && strncmp(snapshot_phase,"sun_",4)==0)style.cull=KichR4SunShadowCull(style.cull,sun_shadow_cull);
            KichR4DeferredDrawDesc d=Description(pipelines[shadow?StaticShadow:StaticGeometry],l.vertex_buffers[mesh.vb],l.index_buffers[mesh.ib],cb,mesh.count,mesh.first,style);
            d.textures[0]=shadow && (described?kich_r4_material::AlphaFunction(material,true):m.alpha_func)==8?0:l.Texture(s,m.texture);
            if(described){kich_r4_material::Bind(s,l,material,mesh.available_attributes,c,d,shadow,!kich_r4_material::Terrain(material),mesh.tree?(!shadow?112u:0u)|(material.tree_wave?128u:0u):0u);
                if(mesh.tree && !shadow){
                    for(unsigned a=0;a<4;++a){c.color_scale[a]=mesh.tree_scale[a]*base.tree_wind[3];c.color_bias[a]=mesh.tree_bias[a]*base.tree_wind[3];}
                    c.sun_scale_bias[0]=mesh.tree_scale[4]*base.tree_wind[3];c.sun_scale_bias[1]=mesh.tree_bias[4]*base.tree_wind[3];}}
            if(described && !shadow)kich_r4_material::BindTerrain(s,l,material,mesh.available_attributes,c,d);
            if(Constants(s,l,c)<0)return s.failure;
            SnapshotResolvedTexture(l,0,m.texture.c_str(),d.textures[0]);
            if(l.Expect(s,"G3c static material draw",api.draw(s.device,&d))<0)return s.failure;
            if(!shadow && kich_r4_record::Enabled()){
                const bool terrain=described && kich_r4_material::Terrain(material),layers=terrain && kich_r4_material::TerrainLayers(material);
                const uint32_t basis=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
                kich_r4_normal_probe::Input probe;probe.known=described;probe.authored=layers || !material.bump.empty();
                probe.basis=(mesh.available_attributes&basis)==basis;probe.metadata=!material.bump_aux.empty();
                probe.attributes=mesh.available_attributes;probe.flags=uint32_t(c.material[0]);probe.index_count=mesh.count;probe.identity=mesh.material;
                probe.resource=m.shader.c_str();probe.base=m.texture.c_str();probe.bump=material.bump.c_str();probe.auxiliary=material.bump_aux.c_str();
                probe.mapped=(probe.flags&kich_r4_material::SHADER_BUMP)!=0 && d.textures[1] && d.textures[2];
                if(layers){probe.metadata=true;for(unsigned layer=0;layer<4;++layer)probe.metadata=probe.metadata && !material.terrain_bumps[layer].empty();
                    probe.dependency=(probe.flags&kich_r4_material::SHADER_TERRAIN_LAYERS)!=0;
                    probe.mapped=(probe.flags&kich_r4_material::SHADER_TERRAIN_NORMAL)!=0;
                    probe.bump=material.terrain_bumps[0].c_str();probe.auxiliary=material.terrain_mask.c_str();}
                kich_r4_normal_probe::Global().Observe(terrain?kich_r4_normal_probe::Terrain:kich_r4_normal_probe::Static,probe);
            }
            if(shadow)if(kich_r4_shadow_record::Pass* record=kich_r4_shadow_record::Active())record->Submitted(kich_r4_shadow_record::Static,i,mesh.first,mesh.count);
            if(!shadow){++l.visible;++l.submitted_draws;l.submitted_triangles+=mesh.count/3;}
        }if(!shadow)++l.frames;return 0;
    }
    int32_t StaticWallmarks(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_scene::Matrix& vp){
        EnsureStaticPlan(l);
        const kich_r4_level::StaticVisibility& visibility=visibility_plan.Select(vp,l.files.sector?l.files.sector(base.camera):UINT32_MAX);
        static_wallmark_order.clear();
        for(size_t n=0;n<visibility.selected.size();++n){const size_t i=visibility.selected[n];
            if(kich_r4_material::AuthoredWallmark(l.scene.materials[l.scene.draws[i].material].descriptor))static_wallmark_order.push_back(i);}
        // R2 mapWmark order: far to near, separate from ordinary transparency.
        std::stable_sort(static_wallmark_order.begin(),static_wallmark_order.end(),[&](size_t a,size_t b){
            const kich_r4_level::Draw& x=l.scene.draws[a];const kich_r4_level::Draw& y=l.scene.draws[b];
            const float ax=(x.low.x+x.high.x)*.5f-base.camera[0],ay=(x.low.y+x.high.y)*.5f-base.camera[1],az=(x.low.z+x.high.z)*.5f-base.camera[2];
            const float bx=(y.low.x+y.high.x)*.5f-base.camera[0],by=(y.low.y+y.high.y)*.5f-base.camera[1],bz=(y.low.z+y.high.z)*.5f-base.camera[2];
            return ax*ax+ay*ay+az*az>bx*bx+by*by+bz*bz;});
        for(size_t n=0;n<static_wallmark_order.size();++n){const size_t i=static_wallmark_order[n];kich_r4_level::Draw mesh=l.scene.draws[i];
            const kich_r4_level::Material& m=l.scene.materials[mesh.material];const kich_r4_material::Descriptor& material=m.descriptor;
            if(mesh.lod){const kich_r4_level::DrawRange range=kich_r4_level::SelectDrawRange(l.scene,mesh,i<static_lods.size()?static_lods[i]:1.f);mesh.first=range.first;mesh.count=range.count;}
            kich_r4_lighting::Constants c=base;c.world=mesh.world;c.mvp=vp;c.normal_matrix=static_normals[i];
            c.material[0]=512;c.alpha[0]=material.alpha_ref;c.alpha[1]=float(material.alpha_func);c.alpha[3]=3; // R2 simple.ps: texture only.
            kich_r4_capture::Style style;kich_r4_material::Apply(material,style);
            // Albedo-only projection retains receiver normal, sun/hemi, gloss,
            // baked irradiance and depth used later to reconstruct position.
            style.write=0;style.surface.color_mask=7;KichR4ReceiverState(style.surface,false);
            KichR4DeferredDrawDesc d=Description(pipelines[StaticWallmarkPipeline],l.vertex_buffers[mesh.vb],l.index_buffers[mesh.ib],cb,mesh.count,mesh.first,style);
            d.textures[0]=l.Texture(s,m.texture);
            if(g_render_snapshot_active){SnapshotStatic(l,i,"R4_STATIC_WALLMARK_ALBEDO",&mesh);SnapshotVisibility("SELECTED","CAMERA_FRUSTUM; RECEIVER_ALBEDO; BOUNDED_WMARK_SHIFT");}
            SnapshotResolvedTexture(l,0,m.texture.c_str(),d.textures[0]);
            if(Constants(s,l,c)<0 || l.Expect(s,"E1.4 authored wallmark albedo",api.draw(s.device,&d))<0)return s.failure;
            ++l.visible;++l.submitted_draws;l.submitted_triangles+=mesh.count/3;
        }return 0;
    }
    int32_t StaticTransparent(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_scene::Matrix& vp){
        EnsureStaticPlan(l);
        const kich_r4_level::StaticVisibility& visibility=visibility_plan.Select(vp,l.files.sector?l.files.sector(base.camera):UINT32_MAX);
        std::vector<size_t> order;for(size_t n=0;n<visibility.selected.size();++n){const size_t i=visibility.selected[n];const kich_r4_level::Material& material=l.scene.materials[l.scene.draws[i].material];
            if(kich_r4_material::AuthoredWallmark(material.descriptor))continue;
            if(material.descriptor.semantic!=kich_r4_material::UNKNOWN?(material.descriptor.passes&kich_r4_material::PASS_FORWARD)!=0:material.kind>=2)order.push_back(i);}
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
            const kich_r4_level::Draw& x=l.scene.draws[a];const kich_r4_level::Draw& y=l.scene.draws[b];
            const float ax=(x.low.x+x.high.x)*.5f-base.camera[0],ay=(x.low.y+x.high.y)*.5f-base.camera[1],az=(x.low.z+x.high.z)*.5f-base.camera[2];
            const float bx=(y.low.x+y.high.x)*.5f-base.camera[0],by=(y.low.y+y.high.y)*.5f-base.camera[1],bz=(y.low.z+y.high.z)*.5f-base.camera[2];
            return ax*ax+ay*ay+az*az>bx*bx+by*by+bz*bz;});
        for(size_t sorted=0;sorted<order.size();++sorted){const size_t i=order[sorted];kich_r4_level::Draw mesh=l.scene.draws[i];const kich_r4_level::Material& m=l.scene.materials[mesh.material];
            const bool described=m.descriptor.semantic!=kich_r4_material::UNKNOWN;
            if(described?!(m.descriptor.passes&kich_r4_material::PASS_FORWARD):m.kind<2)continue;
            if(mesh.lod){const kich_r4_level::DrawRange range=kich_r4_level::SelectDrawRange(l.scene,mesh,i<static_lods.size()?static_lods[i]:1.f);mesh.first=range.first;mesh.count=range.count;mesh.available_attributes=range.available_attributes;}
            if(g_render_snapshot_active){SnapshotStatic(l,i,"R4_STATIC_TRANSPARENT",&mesh);SnapshotVisibility("CANDIDATE_CLASSIFIED","CAMERA_FRUSTUM");}

            if(g_render_snapshot_active)SnapshotVisibility("SELECTED","CAMERA_FRUSTUM");
            kich_r4_lighting::Constants c=base;c.world=mesh.world;c.mvp=vp;c.normal_matrix=static_normals[i];c.alpha[0]=m.alpha_ref;c.alpha[1]=static_cast<float>(m.alpha_func);
            kich_r4_capture::Style style;style.blend=m.blend;style.src=m.source;style.dst=m.destination;style.write=0;style.cull=KICH_R4_CULL_NONE;style.fog_enabled=m.descriptor.fog_enabled;if(described){kich_r4_material::Apply(m.descriptor,style);c.alpha[0]=m.descriptor.alpha_ref;c.alpha[1]=float(m.descriptor.alpha_func);c.material[3]=kich_r4_material::MaterialClass(m.descriptor.material);c.ambient[3]=m.descriptor.emissive_gain;c.hemi[3]=m.descriptor.emissive_specular;}
            c.fog_params[2]=style.fog_enabled?(style.blend && style.dst==2?2.f:1.f):0.f;
            if(Constants(s,l,c)<0)return s.failure;
            KichR4DeferredDrawDesc d=Description(pipelines[described && m.descriptor.semantic==kich_r4_material::EMISSIVE?StaticPlain:m.kind==2?StaticWater:StaticForward],l.vertex_buffers[mesh.vb],l.index_buffers[mesh.ib],cb,mesh.count,mesh.first,style);d.textures[0]=l.Texture(s,m.texture);d.textures[1]=scene_color;d.textures[2]=sun_map;d.textures[3]=sun_far_map;d.textures[6]=sun_mask?sun_mask:white;
            if(base.shadow[2].m[15]>.5f)d.textures[7]=sun_contact_map;
            {const KichR4Handle lut=l.MaterialLut(s);if(!lut)return s.failure;kich_r4_material_lut::Bind(d,lut);}
            SnapshotResolvedTexture(l,0,m.texture.c_str(),d.textures[0]);
            const bool simple=simple_transparent.count(mesh.material)!=0;
            int32_t hr=KICH_R4_OK;
            if(!simple)hr=api.draw(s.device,&d);
            if(simple || hr==KICH_R4_E_ARGUMENT){
                if(!simple){simple_transparent.insert(mesh.material);
                    l.Write("[R4 native B5] transparent compatibility: shader=%s texture=%s kind=%u HRESULT=0x%08X blend=%u/%u/%u; using basic R4 alpha surface",
                        m.shader.c_str(),m.texture.c_str(),m.kind,static_cast<uint32_t>(hr),m.blend,m.source,m.destination);}
                if(!described)c.alpha[1]=8;
                c.alpha[3]=0;if(Constants(s,l,c)<0)return s.failure;
                d.state.draw.pipeline=pipelines[StaticPlain];
                if(!described){d.state.blend_enable=1;d.state.source_blend=5;d.state.dest_blend=6;}
                d.textures[1]=d.textures[2]=0;hr=api.draw(s.device,&d);
            }
            if(l.Expect(s,"B5 static transparent surface",hr)<0)return s.failure;
        }return 0;
    }
    enum ModelPass {GeometryModels,ShadowModels,ForwardBase,ForwardAdd,HUD,Environment,MenuPP,MenuOverlay,DistortionModels,Celestial,CloudLayer,Wallmarks};
    void PrepareStreamBounds(const kich_r4_capture::Batch& batch){
        if(bounded_batch==&batch && stream_bounds.size()==batch.commands.size())return;
        bounded_batch=&batch;stream_bounds.assign(batch.commands.size(),kich_r4_visibility::Bounds());
        for(size_t i=0;i<batch.commands.size();++i){const kich_r4_capture::Command& c=batch.commands[i];
            if(c.layer || !c.count || c.first>batch.indices.size() || c.count>batch.indices.size()-c.first)continue;
            kich_r4_visibility::Bounds bounds;bool finite=true;
            for(uint32_t j=0;j<c.count && finite;++j){const uint32_t index=batch.indices[c.first+j];if(index>=batch.vertices.size()){finite=false;break;}
                const float* point=batch.vertices[index].world;
                for(unsigned a=0;a<3;++a){if(!kich_r4_lighting::Finite(point[a])){finite=false;break;}
                    if(!j || point[a]<bounds.min[a])bounds.min[a]=point[a];
                    if(!j || point[a]>bounds.max[a])bounds.max[a]=point[a];}}
            if(finite){bounds.valid=true;stream_bounds[i]=bounds;}
        }
    }
    bool StreamAffects(const kich_r4_visibility::Bounds& bounds,const kich_r4_lighting::Constants& frame,ModelPass pass)const{
        if(!bounds.valid)return true;
        if(pass==ShadowModels)return kich_r4_visibility::Visible(bounds.min,bounds.max,frame.mvp) &&
            (strncmp(snapshot_phase,"sun_",4)!=0 || visibility_plan.sun_casters.Visible(bounds.min,bounds.max));
        if(pass==ForwardAdd && frame.light_position[3]>0){double distance=0;
            for(unsigned a=0;a<3;++a){const double p=frame.light_position[a],delta=p<bounds.min[a]?bounds.min[a]-p:p>bounds.max[a]?p-bounds.max[a]:0;distance+=delta*delta;}
            return distance<=double(frame.light_position[3])*frame.light_position[3];}
        return true;
    }
    int32_t Models(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const kich_r4_capture::Batch* batch,
        ModelPass pass,const kich_r4_lighting::Constants& frame,const KichR4Handle* maps=NULL){
        if(!batch)return 0;
        if(native_mode && (pass==ShadowModels || pass==ForwardAdd))PrepareStreamBounds(*batch);
        KichR4ModelSession::Pass model_pass=KichR4ModelSession::PassCount;
        if(pass==GeometryModels)model_pass=KichR4ModelSession::Geometry;
        else if(pass==ShadowModels)model_pass=KichR4ModelSession::Shadow;
        else if(pass==ForwardBase)model_pass=KichR4ModelSession::Forward;
        else if(pass==ForwardAdd)model_pass=KichR4ModelSession::ForwardAdd;
        else if(pass==DistortionModels)model_pass=KichR4ModelSession::Distortion;
        const bool mixed=native_mode && (pass==ForwardBase || pass==ForwardAdd || pass==DistortionModels);
        const size_t stream_count=batch->commands.size();
        const size_t total=stream_count+(mixed?batch->native_models.size():0);
        if(ordered_batch!=batch || memcmp(ordered_camera,frame.camera,12)!=0){ordered_batch=batch;memcpy(ordered_camera,frame.camera,12);mixed_order_ready=false;}
        std::vector<size_t>& order=mixed?mixed_order:plain_order;
        if(order.size()!=total){order.resize(total);for(size_t i=0;i<total;++i)order[i]=i;if(mixed)mixed_order_ready=false;}
        if(mixed && !mixed_order_ready){for(size_t i=0;i<order.size();++i)order[i]=i;
            std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
            auto distance=[&](size_t index)->double{
                if(index>=stream_count)return KichR4ModelSession::Distance(batch->native_models[index-stream_count],frame);
                const kich_r4_capture::Command& c=batch->commands[index];
                if(!c.count || c.first>=batch->indices.size())return 0;
                const uint32_t v=batch->indices[c.first];if(v>=batch->vertices.size())return 0;
                double d=0;for(unsigned j=0;j<3;++j){const double delta=double(batch->vertices[v].world[j])-frame.camera[j];d+=delta*delta;}return d;};
            return distance(a)>distance(b);});mixed_order_ready=true;++forward_sorts;}
        struct ModelPhaseGuard {KichR4ModelSession* session;~ModelPhaseGuard(){if(session)session->EndPhase();}} phase_guard={mixed?&native_models:NULL};
        if(mixed)native_models.BeginPhase(snapshot_phase);
        for(unsigned layer=0;layer<3;++layer)for(size_t sorted=0;sorted<order.size();++sorted){
            if(order[sorted]>=stream_count){const uint32_t index=static_cast<uint32_t>(order[sorted]-stream_count);
                if(batch->native_models[index].layer==layer && native_models.Draw(s,l,cb,frame,*batch,model_pass,snapshot_pass_mask,snapshot_phase,maps,0,index)<0)return s.failure;
                continue;}
            const kich_r4_capture::Command& c=batch->commands[order[sorted]];if(c.layer!=layer)continue;
            if(native_mode && ((pass==Wallmarks)!=(c.effect==6)))continue;
            if((pass==Celestial)!=(native_mode && c.effect==kich_r4_optics::CelestialSource))continue;
            if(pass==GeometryModels && !Opaque(c))continue;
            if(pass==ShadowModels && (!Opaque(c) || c.style.emissive_gain>0))continue;
            if((pass==ForwardBase || pass==ForwardAdd) && (c.layer==2 || Opaque(c)))continue;
            if(pass==HUD && c.layer!=2)continue;
            if(native_mode){if(pass==DistortionModels){if(c.effect!=5)continue;}
                else if(c.effect==5)continue;
                if(pass==MenuPP){if(c.effect!=7)continue;}
                else if(pass==MenuOverlay){if(c.effect!=8)continue;}
                else if(c.effect==7 || c.effect==8)continue;
                if(pass==Environment && c.effect!=2)continue;
                if(pass!=Environment && c.effect==2)continue;
                if(pass==CloudLayer && c.effect!=3)continue;
                if(pass!=CloudLayer && c.effect==3)continue;
                if(pass==ForwardAdd && c.effect && c.effect!=6)continue;}
            else if(pass==Environment)continue;
            if(native_mode && !c.layer && (pass==ShadowModels || pass==ForwardAdd) && !StreamAffects(stream_bounds[order[sorted]],frame,pass))continue;
            kich_r4_lighting::Constants constants=frame;constants.alpha[0]=kich_r4_material::AlphaReference(c.style,pass==ShadowModels);constants.alpha[1]=static_cast<float>(kich_r4_material::AlphaFunction(c.style,pass==ShadowModels));constants.alpha[2]=c.style.blend?1.f:0.f;
            if(native_mode && (c.effect==2 || c.effect==3 || c.effect==1 || c.effect==5 || c.effect==7 || pass==Celestial))constants.alpha[3]=0;
            if(native_mode && pass==MenuPP)constants.alpha[3]=30;
            if(pass==Wallmarks)constants.alpha[3]=static_cast<float>(c.wallmark_color);
            if(c.style.emissive_gain>0){constants.ambient[3]=c.style.emissive_gain;constants.hemi[3]=c.style.emissive_specular;
                if(pass==GeometryModels)constants.material[0]=float(uint32_t(constants.material[0])|256u);}
            kich_r4_capture::Style style=c.style;
            constants.fog_params[2]=!c.layer && (pass==ForwardBase || pass==ForwardAdd) && c.style.fog_enabled &&
                (c.effect==0 || c.effect==4)?(c.style.blend && c.style.dst==2?2.f:1.f):0.f;
            if(Constants(s,l,constants)<0)return s.failure;
            if(pass==Environment){style.depth=style.write=0;style.fog_enabled=false;}
            if(pass==Wallmarks){style.depth=1;style.write=0;style.surface.color_mask=7;KichR4ReceiverState(style.surface,false);}
            if(pass==Celestial){kich_r4_optics::SkyStyle(style);if(frame.screen[2]==12)style.blend=0;}
            if(native_mode && pass==MenuPP)style.blend=0;
            unsigned pipeline=pass==GeometryModels?ModelGeometry:pass==ShadowModels?ModelShadow:pass==ForwardBase?ForwardSun:pass==ForwardAdd?ForwardLocal:Plain;
            if(native_mode && pass!=ShadowModels && pass!=GeometryModels){if(c.effect==2)pipeline=Sky;else if(c.effect==3)pipeline=Clouds;else if(c.effect==4)pipeline=Water;else if(c.effect==5 || c.effect==7)pipeline=Distortion;else if(c.effect==1)pipeline=Plain;}
            if(pass==Celestial)pipeline=CelestialPipeline;
            if(pass==Wallmarks)pipeline=WallmarkPipeline;
            if(pass==DistortionModels){pipeline=DistortionMask;style.depth=1;style.write=0;style.blend=1;style.src=5;style.dst=6;}
            if(pass==ShadowModels){style.depth=style.write=1;style.blend=style.clamp=0;style.scissor=false;style.min_z=0;style.max_z=1;
                if(strncmp(snapshot_phase,"sun_",4)==0)style.cull=KichR4SunShadowCull(style.cull,sun_shadow_cull);}
            if(pass==ForwardAdd){style.blend=1;style.src=style.dst=2;style.write=0;}
            // A menu batch with a scene-lit command (e.g. water) compiles the scene set now.
            if(!pipelines[pipeline] && StartScene(s,l)<0)return s.failure;
            KichR4DeferredDrawDesc d=Description(pipelines[pipeline],stream.vb,stream.ib,cb,c.count,c.first,style);
            if(pass==ForwardAdd && kich_r4_projector::Enabled(constants))kich_r4_projector::Sampler(d.state.surface);
            if(pipeline==ForwardSun || pipeline==ForwardLocal || pipeline==Water){const KichR4Handle lut=l.MaterialLut(s);if(!lut)return s.failure;kich_r4_material_lut::Bind(d,lut);}
            d.textures[0]=pass==ShadowModels && kich_r4_material::AlphaFunction(c.style,true)==8?0:native_mode && c.effect==2?l.CubeTexture(s,c.texture):c.texture.empty()?white:l.Texture(s,c.texture);
            if(pass==Celestial)d.textures[1]=depth;
            if(!native_mode && pass==HUD && d.textures[0]==l.checker)return s.Record("B4.4 HUD DDS unavailable",KICH_R4_E_UNSUPPORTED);
            if(maps)for(unsigned j=0;j<6;++j)d.textures[j+2]=maps[j];
            if(native_mode && c.effect){if(c.effect==2 || c.effect==3)d.textures[1]=c.secondary.empty()?d.textures[0]:c.effect==2?l.CubeTexture(s,c.secondary):l.Texture(s,c.secondary);
                if(c.effect==4 || c.effect==7)d.textures[1]=scene_color;}
            if(g_render_snapshot_active){try{const char* category=c.console?"CONSOLE":c.layer==2?(c.effect==8?"MENU_OVERLAY":"UI"):c.layer==1?"HUD_MODEL":c.effect==1?"PARTICLE_OR_GLOW":c.effect==2 || c.effect==3?"ENVIRONMENT":"DYNAMIC_MODEL";
                RenderSnapshot_Context(category,c.resource.empty()?"UNKNOWN":c.resource.c_str(),c.shader.empty()?"UNKNOWN":c.shader.c_str());
                if(c.provenance)RenderSnapshot_SetProvenance(c.provenance.get());
                else{char mesh[160],state[256];snprintf(mesh,sizeof(mesh),"frame_capture/command_%u/first_%u/count_%u",static_cast<unsigned>(order[sorted]),c.first,c.count);
                    snprintf(state,sizeof(state),"blend=%u/%u/%u;depth=%u/%u;cull=%u;alpha=%u/%.9g;effect=%u",c.style.blend,c.style.src,c.style.dst,c.style.depth,c.style.write,c.style.cull,c.style.alpha_func,c.style.alpha_ref,c.effect);
                    const std::string material=KichR4DetailSession::MaterialKey(c.shader+"\n"+c.texture+"\n"+c.secondary+"\n"+state);
                    const char* semantic=c.console?"CONSOLE":c.effect==1?"UNKNOWN":c.effect==2 || c.effect==3?"ENVIRONMENT":c.effect==4?"WATER":c.layer==2?"UI":"DYNAMIC_MODEL";
                    RenderSnapshot_Object(semantic,mesh,material.c_str(),c.effect==1?"EFFECT_1_PARTICLE_OR_GLOW_AMBIGUOUS":"CAPTURE_COMMAND_FALLBACK",0);
                    float uv[8]={};uint32_t samples=0;for(uint32_t j=0;j<c.count && samples<4 && uint64_t(c.first)+j<batch->indices.size();++j){const uint32_t v=batch->indices[c.first+j];if(v<batch->vertices.size()){uv[samples*2]=batch->vertices[v].uv[0];uv[samples*2+1]=batch->vertices[v].uv[1];++samples;}}
                    RenderSnapshot_UV(uv,samples,"CAPTURE_INDEXED_BASE_UV");}
                char queue[48];snprintf(queue,sizeof(queue),"R4_LAYER_%u_EFFECT_%u_MODEL_PASS_%u",c.layer,c.effect,static_cast<unsigned>(pass));
                RenderSnapshot_Queue(queue);RenderSnapshot_PassMask(snapshot_pass_mask);
                RenderSnapshot_Texture(0,c.texture.c_str());RenderSnapshot_Texture(1,c.secondary.c_str());RenderSnapshot_Skin(c.skin.empty()?"CPU_CAPTURE":c.skin.c_str());
                }catch(...){RenderSnapshot_Context("UNKNOWN","NOT_AVAILABLE","NOT_AVAILABLE");RenderSnapshot_PassMask(snapshot_pass_mask);RenderSnapshot_Text("TELEMETRY_LOSS","r4_model_provenance","ALLOCATION_FAILED");}}
            SnapshotResolvedTexture(l,0,c.texture.c_str(),d.textures[0]);
            if(l.Expect(s,"B4.4 model/forward/UI draw",api.draw(s.device,&d))<0)return s.failure;
            if(pass==ShadowModels)if(kich_r4_shadow_record::Pass* record=kich_r4_shadow_record::Active())record->Submitted(kich_r4_shadow_record::Stream,0,c.first,c.count,false);
            if(g_render_snapshot_active && pass==Celestial){
                char id[64];snprintf(id,sizeof(id),"source_%u",static_cast<unsigned>(order[sorted]));
                kich_r4_telemetry::Text("CELESTIAL",id,"mask",frame.screen[2]>=11?"DIAGNOSTIC_UNMASKED":"GBUFFER_DEPTH_INTEGER_LOAD; DISCARD_DEPTH_LT_1; NO_DSV; STENCIL_NOT_USED");
                kich_r4_telemetry::U64("CELESTIAL",id,"debug_mode",static_cast<uint32_t>(frame.screen[2]));
                kich_r4_telemetry::Text("CELESTIAL",id,"stage",frame.screen[2]>=10?"ISOLATED_FINAL_COLOR_NO_POSTPROCESS":"AFTER_SKY_BEFORE_COMBINE_CLOUDS_AND_FORWARD");
                kich_r4_telemetry::Text("CELESTIAL",id,"texture",c.texture.c_str());
                kich_r4_telemetry::Text("CELESTIAL",id,"execution","DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED");
            }
            if(g_render_snapshot_active && native_mode && (c.effect==2 || c.effect==3 || c.effect==4 || c.effect==5 || c.effect==7)){
                using namespace kich_r4_telemetry;char id[96];snprintf(id,sizeof(id),"command_%u_effect_%u_pass_%u",static_cast<unsigned>(order[sorted]),c.effect,static_cast<unsigned>(pass));
                const char* category=c.effect==2 || c.effect==3?"ENVIRONMENT":"POSTPROCESS";
                Text(category,id,"kind",c.effect==2?"SKY":c.effect==3?"CLOUDS":c.effect==4?"WATER":c.effect==5?"DISTORTION_MASK":"MENU_LENS");
                Text(category,id,"execution","DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED");Text(category,id,"texture0",c.texture.empty()?"$white":c.texture.c_str());Text(category,id,"texture1",c.secondary.c_str());
                U64(category,id,"input0_handle",d.textures[0]);U64(category,id,"input1_handle",d.textures[1]);U64(category,id,"pipeline_handle",pipelines[pipeline]);
                U64(category,id,"blend",style.blend);U64(category,id,"source_blend",style.src);U64(category,id,"dest_blend",style.dst);U64(category,id,"depth_test",style.depth);U64(category,id,"depth_write",style.write);U64(category,id,"cull",style.cull);
                Text(category,id,"sampling",c.effect==2?"CUBEMAP_DIRECTION_FROM_CAPTURE_NORMAL; SKY0/SKY1_LERP_VERTEX_ALPHA; RGB_TIMES_VERTEX_RGB_TIMES_2":c.effect==3?"TEXTURE0_BASE_UV_PLUS_TEXTURE1_CAPTURE_NORMAL_XY; VERTEX_RGBA_MODULATION":"SEE_SELECTED_NATIVE_SHADER");
                if(c.effect==2 || c.effect==3)RenderSnapshot_Text("ENVIRONMENT","sky_cloud_execution","SUCCESSFUL_DRAW_RECORDS_PRESENT; NOT_PIXEL_VISIBILITY");
                Vector(category,id,"frame_view_projection",constants.mvp.m,16);Text(category,id,"geometry_transform","CAPTURED_WORLD_AND_CLIP_POSITIONS; SEE_GEOMETRY_PROVENANCE");
            }
            if(pass==GeometryModels || pass==ForwardBase || pass==HUD)++stream.layer_draws[c.layer];
        }
        if(native_mode && !mixed && !batch->native_models.empty() && model_pass!=KichR4ModelSession::PassCount)
            return native_models.Draw(s,l,cb,frame,*batch,model_pass,snapshot_pass_mask,snapshot_phase,maps);
        return 0;
    }
    int32_t PrepareModels(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_capture::Batch& batch){
        if(!native_mode)return 0;
        if(!batch.native_models.empty() && native_models.Start(s,l,KichR4DeferredShaderSource(),shaders[4],shaders[5],shaders[8],shaders[9],shaders[10],shaders[17],white)<0)return s.failure;
        return native_models.Prepare(s,l,batch);
    }
    static bool HasDistortion(const kich_r4_capture::Batch* batch){
        if(!batch)return false;
        for(size_t i=0;i<batch->commands.size();++i)if(batch->commands[i].effect==5)return true;
        for(size_t i=0;i<batch->native_models.size();++i)if(batch->native_models[i].effect==5)return true;
        return false;
    }
    int32_t Distort(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const kich_r4_capture::Batch* batch){
        if(!native_mode || !HasDistortion(batch))return 0;
        SnapshotPass("DISTORTION",RENDER_SNAPSHOT_PASS_DISTORTION,"mask");
        const float neutral[4]={.5f,.5f,0,0};
        if(Pass(s,l,width,height,1,distortion_map,0,depth,KICH_R4_CLEAR_COLOR,neutral)<0 || Models(s,l,stream,batch,DistortionModels,base)<0)return s.failure;
        SnapshotPhase("composite");kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;copy.alpha[3]=0;
        if(Pass(s,l,width,height,1,scene_color,0,0,0)<0 || Fullscreen(s,l,DistortionComposite,copy,final_color,distortion_map,NULL)<0)return s.failure;
        std::swap(scene_color,final_color);return 0;
    }
    int32_t Fullscreen(KichR4FrameSession& s,KichR4LevelSession& l,Pipeline p,const kich_r4_lighting::Constants& c,KichR4Handle t0,KichR4Handle t1,const KichR4Handle* maps,bool add=false){
        if(g_render_snapshot_active){char material[64];snprintf(material,sizeof(material),"r4_deferred/pipeline_%u",static_cast<unsigned>(p));
            RenderSnapshot_Context("FULLSCREEN","GENERATED_TRIANGLE","R4_DEFERRED");RenderSnapshot_Object("FULLSCREEN","r4_generated/fullscreen_triangle",material,"R4_FULLSCREEN_PIPELINE",0);
            RenderSnapshot_Queue("R4_FULLSCREEN");RenderSnapshot_PassMask(snapshot_pass_mask);const float uv[]={0,1,0,-1,2,1};RenderSnapshot_UV(uv,3,"GENERATED_FULLSCREEN_TRIANGLE");
            RenderSnapshot_Skin("NONE");for(unsigned i=0;i<8;++i)RenderSnapshot_Texture(i,"RENDER_TARGET_HANDLE");}
        if(Constants(s,l,c)<0)return s.failure;
        kich_r4_capture::Style style;style.depth=style.write=0;style.cull=KICH_R4_CULL_NONE;style.clamp=1;
        if(p==Postprocess){style.clamp=0; // global clamp would also override the noise sampler
            style.surface.samplers[0]={3,3,3,2,2,2,1,0,0,0};
            style.surface.samplers[1]={1,1,1,2,2,2,1,0,0,0};}
        if(p==Sun || p==SunShafts)style.surface.samplers[6]={1,1,1,2,2,2,1,0,0,0}; // R2 sunmask: wrap, trilinear
        // R2 phase_bloom/combine_2 r_Sampler_clf: clamp, linear, no mips.
        if(p==BloomBuild || p==BloomFilter || p==BloomFilterFast || p==BloomComposite || p==Luminance1 || p==Luminance2 || p==Luminance3)
            style.surface.samplers[0]=style.surface.samplers[1]={3,3,3,2,2,0,1,0,0,0};
        if(add){style.blend=1;style.src=style.dst=2;}
        if(p==Combine && native_mode && c.screen[2]==0){style.blend=1;style.src=6;style.dst=5;}
        if(p==Local){KichR4LocalLight source={};memcpy(source.position,c.light_position,12);source.range=c.light_position[3];
            const kich_r4_visibility::ScreenRect rect=kich_r4_visibility::LightScreenRect(source,base.mvp);
            style.scissor=true;style.rect[0]=int32_t(floorf(rect.left*width));style.rect[1]=int32_t(floorf(rect.top*height));
            style.rect[2]=int32_t(ceilf(rect.right*width));style.rect[3]=int32_t(ceilf(rect.bottom*height));
            if(style.rect[0]>=style.rect[2] || style.rect[1]>=style.rect[3])return 0;
        }
        KichR4DeferredDrawDesc d=Description(pipelines[p],screen_vb,screen_ib,cb,3,0,style);d.textures[0]=t0;d.textures[1]=t1;
        if(p==Local && kich_r4_projector::Enabled(c))kich_r4_projector::Sampler(d.state.surface);
        if(maps)for(unsigned i=0;i<6;++i)d.textures[i+2]=maps[i];
        if(p==SunShafts)d.textures[11]=receiver_depth;
        if(p==Sun || p==Local || p==Combine){d.textures[11]=receiver_depth;
            const KichR4Handle lut=l.MaterialLut(s);if(!lut)return s.failure;kich_r4_material_lut::Bind(d,lut);}
        const int32_t result=l.Expect(s,"B4.4 fullscreen draw",api.draw(s.device,&d));
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;char id[96];snprintf(id,sizeof(id),"%s_pipeline_%u",snapshot_phase,static_cast<unsigned>(p));
            Text("POSTPROCESS",id,"execution",result>=0?"DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED":"DRAW_FAILED");
            U64("POSTPROCESS",id,"pipeline",p);U64("POSTPROCESS",id,"pipeline_handle",pipelines[p]);U64("POSTPROCESS",id,"input0_handle",t0);U64("POSTPROCESS",id,"input1_handle",t1);
            Text("POSTPROCESS",id,"output","SEE_BACKEND_PASS_RENDER_TARGET_HANDLES");U64("POSTPROCESS",id,"additive",add?1:0);}
        return result;
    }
    // P0-C1 R2 phase_luminance: bloom1 256^2 -> 64^2 -> 8^2 -> 1x1 exposure
    // scale, blended with the previous result. MiddleGray lerps from (1,0,1)
    // to (middle gray,1,low lum) by the tonemap amount, so r2_tonemap off
    // converges to 1. The pool swaps at frame end like phase_combine's.
    int32_t Luminance(KichR4FrameSession& s,KichR4LevelSession& l,const KichR4LightingFrame& lighting){
        const float* e=lighting.exposure;
        luminance_adapt=.9f*luminance_adapt+.1f*e[4]*e[3];
        const float none[3]={1,0,1},full[3]={e[1],1,e[2]};
        kich_r4_lighting::Constants c=base;c.alpha[1]=8;c.alpha[3]=0;
        for(unsigned i=0;i<3;++i)c.light_position[i]=none[i]+(full[i]-none[i])*e[0];
        c.light_position[3]=luminance_adapt;
        SnapshotPhase("luminance");
        if(Pass(s,l,64,64,1,lum64,0,0,0)<0 || Fullscreen(s,l,Luminance1,c,bloom1,0,NULL)<0 ||
            Pass(s,l,8,8,1,lum8,0,0,0)<0 || Fullscreen(s,l,Luminance2,c,lum64,0,NULL)<0 ||
            Pass(s,l,1,1,1,lum_pool[1],0,0,0)<0 || Fullscreen(s,l,Luminance3,c,lum8,lum_pool[0],NULL)<0)return s.failure;
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;
            Vector("POSTPROCESS","exposure","amount_middlegray_lowlum_adaptation_dt",e,5);
            Vector("POSTPROCESS","exposure","middlegray_constant",c.light_position,4);
            Text("POSTPROCESS","exposure","contract","R2_LUM_T64_T8_POOL_R32F; SCALE_TWO_FRAMES_OLD_AT_SKY_COMBINE_CLOUDS");}
        return 0;
    }
    // P0-C2 R2 phase_bloom and the combine_2 bloom term. high_color is R2
    // generic1 (sky and combine at colour/8); clouds, forward and HUD stay
    // low-only exactly as in R2. Output replaces final_color with low+bloom.
    int32_t Bloom(KichR4FrameSession& s,KichR4LevelSession& l,const KichR4LightingFrame& lighting){
        const float* b=lighting.bloom;const unsigned size=kich_r4_bloom::Size;
        kich_r4_lighting::Constants c=base;c.alpha[1]=8;c.alpha[3]=0;
        // b_params.x threshold; kich_bloom_params mode, strength, knee.
        c.light_position[0]=b[0];c.light_position[1]=b[4];c.light_position[2]=b[5];c.light_position[3]=b[6];
        SnapshotPass("POSTPROCESS",RENDER_SNAPSHOT_PASS_POSTPROCESS,"bloom_build");
        if(Pass(s,l,size,size,1,bloom1,0,0,0)<0 || Fullscreen(s,l,BloomBuild,c,high_color,0,NULL)<0)return s.failure;
        if(Luminance(s,l,lighting)<0)return s.failure; // R2 phase_luminance runs on the unfiltered build
        if(b[7]>.5f){SnapshotPhase("bloom_filter_fast");
            c.light_position[0]=b[2]; // r2_ls_bloom_kernel_b, in bloom texels
            if(Pass(s,l,size,size,1,bloom2,0,0,0)<0 || Fullscreen(s,l,BloomFilterFast,c,bloom1,0,NULL)<0 ||
                Pass(s,l,size,size,1,bloom1,0,0,0)<0 || Fullscreen(s,l,BloomFilterFast,c,bloom2,0,NULL)<0)return s.failure;}
        else for(unsigned vertical=0;vertical<2;++vertical){SnapshotPhase(vertical?"bloom_filter_y":"bloom_filter_x");
            kich_r4_bloom::FilterWeights(vertical!=0,b[1],b[3],width,height,c.light_position,c.light_direction);
            c.light_color[0]=vertical?0.f:1.f;c.light_color[1]=vertical?1.f:0.f;c.light_color[2]=c.light_color[3]=0;
            if(Pass(s,l,size,size,1,vertical?bloom1:bloom2,0,0,0)<0 || Fullscreen(s,l,BloomFilter,c,vertical?bloom2:bloom1,0,NULL)<0)return s.failure;}
        SnapshotPhase("bloom_composite");
        if(Pass(s,l,width,height,1,scene_color,0,0,0)<0 || Fullscreen(s,l,BloomComposite,c,final_color,bloom1,NULL)<0)return s.failure;
        std::swap(scene_color,final_color);
        std::swap(lum_pool[0],lum_pool[1]); // phase_combine end: this result becomes s_tonemap's source
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;
            Vector("POSTPROCESS","bloom","settings_threshold_kernel_g_b_scale_mode_strength_knee_fast",b,8);
            Text("POSTPROCESS","bloom","contract","R2_GENERIC1_HIGH_DIV_8_RGBA8; BUILD_256_4TAP; GAUSS_WAVE_15TAP_XY_OR_FAST; COMBINE_2_LOW_PLUS_RGB_TIMES_A; TONEMAP_SCALE_1");}
        return 0;
    }
    int32_t DrawMenu(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const KichR4GameFrame& input,const kich_r4_capture::Batch& batch,bool after_scene=false){
        SnapshotCPU(&batch);if(!after_scene){native_models.ResetFrame();details.ResetFrame(input.engine_frame);}SnapshotPass("MENU_UI",RENDER_SNAPSHOT_PASS_MENU_UI,"menu");
        base.screen[0]=static_cast<float>(input.width);base.screen[1]=static_cast<float>(input.height);
        if(!cb && Start(s,l)<0)return s.failure;
        if(ScreenTargets(s,l,input.width,input.height)<0 || (!after_scene && stream.Upload(s,l,batch)<0))return s.failure;
        if(Pass(s,l,input.width,input.height,1,native_mode?scene_color:final_color,0,0,KICH_R4_CLEAR_COLOR)<0 || Models(s,l,stream,&batch,HUD,base)<0)return s.failure;
        if(native_mode){kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;copy.alpha[3]=0;
            // Sample a separate menu image: the lens must never read its own render target.
            if(Pass(s,l,input.width,input.height,1,final_color,0,0,0)<0 || Fullscreen(s,l,Copy,copy,scene_color,0,NULL)<0 || Models(s,l,stream,&batch,MenuPP,base)<0 || Models(s,l,stream,&batch,MenuOverlay,base)<0)return s.failure;}
        return Process(s,l,input,true);
    }
    int32_t Process(KichR4FrameSession& s,KichR4LevelSession& l,const KichR4GameFrame& input,bool menu=false){
        SnapshotPass("POSTPROCESS",RENDER_SNAPSHOT_PASS_POSTPROCESS,"postprocess");
        kich_r4_lighting::Constants c=base;const float* p=input.postprocess;
        c.screen[0]=static_cast<float>(input.width);c.screen[1]=static_cast<float>(input.height);
        memcpy(c.alpha,p,16);memcpy(c.light_position,p+4,16);memset(c.light_direction,0,sizeof(c.light_direction));
        KichR4Handle noise=0;const char* noise_status=menu?"MENU_DISABLED":"DISABLED";
        c.light_position[3]=0;
        if(!menu && kich_r4_noise::Amount(p[7])>0){noise_status="UNAVAILABLE_OR_NOT_RESIDENT";
            const auto found=l.texture_registry.entries.find(kich_r4_noise::Texture());
            if(found!=l.texture_registry.entries.end()){const kich_r4_texture::Entry& texture=found->second;
                if(texture.handle && texture.handle!=l.checker && texture.handle!=l.white && texture.faces==1 && texture.reason==kich_r4_texture::Ready){
                    if(kich_r4_noise::Coordinates(input.width,input.height,texture.width,texture.height,p[9],p[8],c.light_direction)){
                        noise=texture.handle;c.light_position[3]=kich_r4_noise::Amount(p[7]);noise_status="READY";
                    }else noise_status="INVALID_SCALE_OR_DIMENSIONS";}}}
        memcpy(c.sun_color,p+10,12);memcpy(c.ambient,p+13,12);memcpy(c.hemi,p+16,12);memcpy(c.fog,p+19,12);
        if(menu){c.alpha[3]=0;memset(c.light_position,0,sizeof(c.light_position));
            for(unsigned i=0;i<3;++i){c.sun_color[i]=.5f;c.ambient[i]=0;c.hemi[i]=1.f/3.f;}}
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;const char* id="native_postprocess";
            Text("POSTPROCESS",id,"requested_layout","gamma,brightness,contrast,gray,blur,duality_h,duality_v,noise,noise_phase,noise_scale,baseRGB,addRGB,grayRGB,balanceRGB");
            Vector("POSTPROCESS",id,"requested_0_15",p,16);Vector("POSTPROCESS",id,"requested_16_21",p+16,6);
            Vector("POSTPROCESS",id,"applied_gamma_brightness_contrast_gray",c.alpha,4);Vector("POSTPROCESS",id,"applied_blur_duality_noise",c.light_position,4);
            Vector("POSTPROCESS",id,"applied_noise_uv_origin_span",c.light_direction,4);Text("POSTPROCESS",id,"noise_texture",kich_r4_noise::Texture());
            Text("POSTPROCESS",id,"noise_status",noise_status);U64("POSTPROCESS",id,"noise_texture_handle",noise);
            Text("POSTPROCESS",id,"noise_contract","R2_MULTIPLICATIVE; LINEAR_WRAP; SOURCE_DIMENSIONS; INDEPENDENT_UV; QUANTIZED_STRENGTH; NEUTRAL_IF_UNAVAILABLE");
            Vector("POSTPROCESS",id,"applied_baseRGB",c.sun_color,3);Vector("POSTPROCESS",id,"applied_addRGB",c.ambient,3);Vector("POSTPROCESS",id,"applied_grayRGB",c.hemi,3);Vector("POSTPROCESS",id,"applied_balanceRGB",c.fog,3);
            Text("POSTPROCESS",id,"override",menu?"MENU_ZERO_GRAY_BLUR_DUALITY_NOISE; BASE=.5 ADD=0 GRAY=1/3":"NONE");
            Text("POSTPROCESS",id,"shader_parameter_transform","gamma=max(.01,alpha.x); brightness=alpha.y*.5; contrast=alpha.z*.5; SATURATED_OUTPUT");
            Text("POSTPROCESS",id,"HDR_exposure_bloom",menu?"MENU_NO_BLOOM":"R2_BLOOM_COMPOSITED_BEFORE_POSTPROCESS; EXPOSURE_SCALE_1_TONEMAP_ADAPTATION_NOT_YET");Text("POSTPROCESS",id,"output_color_format","SWAPCHAIN_UNORM; NO_HDR_TONEMAP");
            U64("POSTPROCESS",id,"input_handle",final_color);Text("POSTPROCESS",id,"output","SWAPCHAIN_BACKBUFFER");Text("POSTPROCESS",id,"execution","PLANNED");}
        if(Pass(s,l,input.width,input.height,1,0,0,0,0)<0)return s.failure;
        const int32_t result=Fullscreen(s,l,Postprocess,c,final_color,noise,NULL);
        kich_r4_telemetry::Text("POSTPROCESS","native_postprocess","execution",result>=0?"DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED":"DRAW_FAILED");return result;
    }
    kich_r4_lighting::Constants LocalConstants(const KichR4LocalLight& l)const {return LocalConstants(l,local_shadow_size);}
    // size: this light's map resolution (P0-S7 plan), shared by all its faces.
    kich_r4_lighting::Constants LocalConstants(const KichR4LocalLight& l,uint32_t size)const {
        kich_r4_lighting::Constants c=base;c.alpha[3]=1;c.sun_direction[3]=0;memcpy(c.light_position,l.position,12);c.light_position[3]=l.range;
        const kich_r4_scene::Vec3 direction=kich_r4_scene::Normalize(kich_r4_scene::Vec3{l.direction[0],l.direction[1],l.direction[2]});
        c.light_direction[0]=direction.x;c.light_direction[1]=direction.y;c.light_direction[2]=direction.z;c.light_direction[3]=cosf(l.cone*.5f);
        memcpy(c.light_color,l.color,12);c.light_color[3]=static_cast<float>(l.type);c.screen[3]=static_cast<float>(size);
        for(unsigned i=0;i<(l.type==2?1u:6u);++i)c.shadow[i]=kich_r4_lighting::LocalShadow(l,i,size);
        return c;
    }
    void LocalInputs(KichR4LevelSession& level,const KichR4LightingFrame& lighting,unsigned index,kich_r4_lighting::Constants& c,KichR4Handle* maps,bool count=false){
        const KichR4LightProjector* projector=index<lighting.projectors.size()?&lighting.projectors[index]:NULL;
        const char* status=kich_r4_projector::Configure(level,lighting.lights[index],projector,c,maps);
        if(count && lighting.lights[index].type==2){++frame_projector_spots;
            if(projector && !projector->texture.empty()){++frame_projector_named;
                if(kich_r4_projector::Enabled(c))++frame_projector_ready;else ++frame_projector_fallback;}}
        if(g_render_snapshot_active){char id[48];const uint64_t serial=index<lighting.sources.size()?lighting.sources[index].serial:0;
            if(serial)snprintf(id,sizeof(id),"source_%llu",static_cast<unsigned long long>(serial));else snprintf(id,sizeof(id),"local_%u",index);
            kich_r4_telemetry::Text("LIGHT",id,"projector_status",status);
            kich_r4_telemetry::Text("LIGHT",id,"projector_texture",projector?projector->texture.c_str():"");
            kich_r4_telemetry::U64("LIGHT",id,"projector_handle",kich_r4_projector::Enabled(c)?maps[kich_r4_projector::MapIndex]:0);
            if(projector)kich_r4_telemetry::Vector("LIGHT",id,"projector_right",projector->right,3);
            if(kich_r4_projector::Enabled(c))kich_r4_telemetry::Vector("LIGHT",id,"projector_world_to_clip",c.shadow[kich_r4_projector::MatrixIndex].m,16);
            kich_r4_telemetry::Text("LIGHT",id,"projector_contract","SPOT_T3; SHADOW_1_MATRIX; CLAMP_LINEAR_NO_MIP; RGBA; INDEPENDENT_OF_SHADOW_SELECTION");}
    }
    double model_prepare_ms=0;
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const KichR4GameFrame& input,const kich_r4_capture::Batch* captured,uint32_t debug){
        frame_sun_cascades=frame_spot_shadow_lights=frame_point_shadow_lights=frame_local_shadow_faces=0;
        frame_projector_spots=frame_projector_named=frame_projector_ready=frame_projector_fallback=0;
        shadow_record.Begin(input.engine_frame);
        if(!native_mode)l.texture_registry.Begin(); // Native presenter owns its submission epoch.
        if(!native_mode || visibility_frame!=input.engine_frame || visibility_revision!=l.revision)BeginFramePlan(l,input);
        ordinary_profile=ordinary_profile && debug==0; // Presenter explicitly requests sparse normal-frame GPU probes.
        SnapshotCPU(captured);native_models.ResetFrame();details.ResetFrame(input.engine_frame);
        if(g_render_snapshot_active){RenderSnapshot_U64("LIGHT_REGISTRY","selected_frame_inputs",input.lighting.count);RenderSnapshot_Text("ENVIRONMENT","source","COPIED_CURRENT_ENV_LIGHTING_FRAME");
            kich_r4_telemetry::Vector("ENVIRONMENT","weather","sun_direction",input.lighting.sun_direction,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","sun_color",input.lighting.sun_color,3);
            kich_r4_telemetry::Vector("ENVIRONMENT","weather","ambient",input.lighting.ambient,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","hemi",input.lighting.hemi,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","fog",input.lighting.fog,3);
            RenderSnapshot_Text("ENVIRONMENT","sky_cloud_execution","NO_SUCCESSFUL_DRAW_RECORDED_YET");RenderSnapshot_Text("ENVIRONMENT","weather_texture_names","SEE_ENVIRONMENT_COMMAND_TEXTURE_RECORDS; NOT_DERIVED_FROM_LIGHTING_VALUES");
            RenderSnapshot_Text("ENVIRONMENT","fog_applied","CURRENT_ENV_RADIAL_DISTANCE; R2_FOG_SQUARED_SKY_BLEND; FORWARD_SOURCE_FOG_PLANE; HUD_UI_EXCLUDED");
            RenderSnapshot_Value("ENVIRONMENT","fog_near",input.lighting.fog_near);RenderSnapshot_Value("ENVIRONMENT","fog_far",input.lighting.fog_far);RenderSnapshot_U64("ENVIRONMENT","fog_valid",input.lighting.fog_valid);
            RenderSnapshot_Text("ENVIRONMENT","ambient_hemi_applied","DEFERRED_COMBINE_R2_HMODEL; FORWARD_RAW_AMBIENT+hemi*vertex_hemi_response");
            kich_r4_telemetry::Vector("ENVIRONMENT","hmodel","L_ambient",input.lighting.env_ambient,3);kich_r4_telemetry::Vector("ENVIRONMENT","hmodel","env_color",input.lighting.env_color,4);
            RenderSnapshot_U64("ENVIRONMENT","hmodel_inputs_valid",input.lighting.env_valid);RenderSnapshot_Value("ENVIRONMENT","r2_gloss_factor",input.lighting.gloss_factor);
            RenderSnapshot_Text("ENVIRONMENT","sun_mode",input.lighting.sun_mode==KICH_R4_SUN_STATIC?"R2_STATIC":input.lighting.sun_mode==KICH_R4_SUN_DISABLED?"DISABLED":
                input.lighting.sun_mode==KICH_R4_SUN_DYNAMIC?"R2_DYNAMIC":"DEFAULT_DYNAMIC");
            RenderSnapshot_Text("ENVIRONMENT","sun_disk_direction_link","NOT_INSTRUMENTED; LIGHT_DIRECTION_AND_SHADOW_PROJECTION_RECORDED_SEPARATELY");
            RenderSnapshot_U64("TRANSPARENCY","debug_mode",debug);RenderSnapshot_Text("TRANSPARENCY","debug_view",kich_r4_optics::DebugView(debug));RenderSnapshot_Text("TRANSPARENCY","forward_requested",!debug?"ENABLED":"DISABLED_BY_DEBUG_VIEW");
            RenderSnapshot_Text("TRANSPARENCY","sorting","STABLE_BACK_TO_FRONT_DISTANCE; SHARED_STREAM_AND_NATIVE_MODEL_QUEUE");RenderSnapshot_Text("TRANSPARENCY","distortion_requested",native_mode && HasDistortion(captured)?"HAS_COMMANDS":"NO_COMMANDS_OR_UNSUPPORTED");}
        Mark(s,0);
        if(!input.lighting.valid || input.lighting.count>4096 || input.lighting.count>input.lighting.lights.size() || input.lighting.count>input.lighting.sources.size())return s.Record("B4.4 weather snapshot",KICH_R4_E_ARGUMENT);
        const float* weather[]={input.lighting.sun_direction,input.lighting.sun_color,input.lighting.ambient,input.lighting.hemi,input.lighting.fog};
        for(unsigned i=0;i<5;++i)for(unsigned j=0;j<3;++j)if(!kich_r4_lighting::Finite(weather[i][j]))return s.Record("B4.4 weather values",KICH_R4_E_ARGUMENT);
        for(unsigned i=0;i<3;++i)if(!kich_r4_lighting::Finite(input.position[i]))return s.Record("B4.4 camera position",KICH_R4_E_ARGUMENT);
        for(unsigned i=0;i<input.lighting.count;++i)if(!kich_r4_lighting::ValidLight(input.lighting.lights[i]))return s.Record("B4.4 local light",KICH_R4_E_ARGUMENT);
        if((!cb && Start(s,l)<0) || StartScene(s,l)<0)return s.failure;
        if(ScreenTargets(s,l,input.width,input.height)<0)return s.failure;
        if(native_mode && details.source){
            if(details.Start(s,l,KichR4DeferredShaderSource(),shaders[4],shaders[5])<0 || details.Upload(s,l,input.engine_frame)<0)return s.failure;}
        if(captured && stream.Upload(s,l,*captured)<0)return s.failure;
        const auto model_started=kich_r4_metrics::Start(g_render_snapshot_active);
        if(captured && PrepareModels(s,l,*captured)<0)return s.failure;
        model_prepare_ms=kich_r4_metrics::Elapsed(model_started);
        memset(&base,0,sizeof(base));base.material[3]=1;base.world=base.normal_matrix=kich_r4_scene::Identity();memcpy(base.mvp.m,input.view_projection,64);
        if(!kich_r4_lighting::Inverse(base.mvp,base.inverse_vp,input.position))return s.Record("B4.4 relative inverse camera",KICH_R4_E_ARGUMENT);
        kich_r4_lighting::ReceiverInverse(base.mvp,base.receiver_inverse,input.position);
        memcpy(base.tree_wave,input.tree_wave,16);memcpy(base.tree_wind,input.tree_wind,16);base.tree_wind[3]=input.tree_sbc;
        memcpy(base.camera,input.position,12);base.camera[3]=150;base.light_position[3]=input.time;base.screen[0]=static_cast<float>(width);base.screen[1]=static_cast<float>(height);base.screen[2]=static_cast<float>(debug);
        // P0-S5 r4_sun_shadow_size: the contact and 128 m cascades. P0-S7
        // r4_sun_far_size: the native 512 m cascade. PCFWide reads each map's size.
        const uint32_t sun_size=kich_r4_lighting::SunMapSize(input.lighting);base.screen[3]=static_cast<float>(sun_size);
        const uint32_t far_size=native_mode?kich_r4_lighting::SunFarMapSize(input.lighting):sun_size;
        if(sun_size!=sun_map_size){KichR4Handle* maps[2]={&sun_map,&sun_contact_map};
            for(unsigned i=0;i<2;++i)if(*maps[i]){if(l.Expect(s,"P0-S5 sun map resize",l.bridge.resources.release_resource(s.device,*maps[i]))<0)return s.failure;*maps[i]=0;}
            sun_map_size=sun_size;}
        if(far_size!=sun_far_map_size){
            if(sun_far_map){if(l.Expect(s,"P0-S7 far sun map resize",l.bridge.resources.release_resource(s.device,sun_far_map))<0)return s.failure;sun_far_map=0;}
            sun_far_map_size=far_size;}
        const kich_r4_scene::Vec3 dir=kich_r4_scene::Normalize(kich_r4_scene::Vec3{weather[0][0],weather[0][1],weather[0][2]});
        base.sun_direction[0]=dir.x;base.sun_direction[1]=dir.y;base.sun_direction[2]=dir.z;
        memcpy(base.sun_color,weather[1],12);memcpy(base.ambient,weather[2],12);memcpy(base.hemi,weather[3],12);memcpy(base.fog,weather[4],12);
        kich_r4_lighting::FogConstants(input.lighting,base.mvp,base);
        const bool sun=kich_r4_visibility::SunEnabled(input.lighting);
        // HLSL sun_color.w: 0 no sun, 1 dynamic shadow maps, 2 R2 static sun
        // (USE_R2_STATIC_SUN: baked G-buffer occlusion*2 and no sun maps).
        const bool static_sun=input.lighting.sun_mode==KICH_R4_SUN_STATIC && kich_r4_scene::Dot(dir,dir)>.5f;
        base.sun_color[3]=sun?1.f:static_sun?2.f:0.f;
        // phase_combine hmodel() inputs; direct/test producers keep raw ambient
        // and no environment contribution rather than an invented scale.
        if(input.lighting.env_valid){memcpy(base.env_ambient,input.lighting.env_ambient,12);memcpy(base.env_color,input.lighting.env_color,16);}
        else memcpy(base.env_ambient,base.ambient,12);
        base.env_ambient[3]=input.lighting.env_valid?input.lighting.gloss_factor:1.f;
        if(sun && input.lighting.sunmask_valid){memcpy(base.sunmask_u,input.lighting.sunmask,16);memcpy(base.sunmask_v,input.lighting.sunmask+4,16);}
        if(native_mode){
            // R2 clears the luminance pool to 0x7f7f7f7f (127/255) once at creation.
            if(!lum_ready){const float initial[4]={127.f/255,127.f/255,127.f/255,127.f/255};
                for(unsigned i=0;i<2;++i)if(Pass(s,l,1,1,1,lum_pool[i],0,0,KICH_R4_CLEAR_COLOR,initial)<0)return s.failure;
                lum_ready=true;}
            // light_color.w: sky, combine and clouds read the R2 s_tonemap scale at t7.
            base.light_color[3]=1;}
        KichR4Handle sun_inputs[6]={};
        sun_shadow_cull=native_mode?input.lighting.sun_shadow_cull:0;native_models.sun_shadow_cull=sun_shadow_cull;
        if(sun){
            // P0-S2: optional contact cascade (shadow[2], t7) inside the unchanged 128/512 m pair.
            kich_r4_scene::Matrix contact={};
            const bool contact_cascade=native_mode && kich_r4_lighting::SunContactShadow(input.lighting,contact);
            const unsigned cascades=native_mode?(contact_cascade?3:2):1;
            base.shadow[0]=kich_r4_lighting::SunCascade(input.lighting,input.position,native_mode?128.f:512.f);
            base.shadow[1]=kich_r4_lighting::SunCascade(input.lighting,input.position,512.f,far_size);
            if(contact_cascade)base.shadow[2]=contact;
            const float spans[3]={native_mode?128.f:512.f,512.f,contact_cascade?input.lighting.sun_contact[3]:0.f};
            KichR4Handle* targets[3]={&sun_map,&sun_far_map,&sun_contact_map};const unsigned slots[3]={0,1,5}; // t2, t3, t7
            const uint32_t sizes[3]={sun_size,far_size,sun_size};
            for(unsigned cascade=0;cascade<cascades;++cascade){KichR4Handle& map=*targets[cascade];const uint32_t size=sizes[cascade];
                if(!map){if(Target(s,l,map,size,size,KICH_R4_DEPTH32F,true)<0)return s.failure;++sun_map_generation[cascade];}
                kich_r4_shadow_record::Pass* record=shadow_record.Sun(cascade,map,sun_map_generation[cascade],size,spans[cascade],base.shadow[cascade].m);
                kich_r4_shadow_record::Scope record_scope(record);
                char phase[24];snprintf(phase,sizeof(phase),"sun_%u",cascade);SnapshotPass("SUN_SHADOW",RENDER_SNAPSHOT_PASS_SUN_SHADOW,phase);
                if(g_render_snapshot_active){RenderSnapshot_Light("sun","SCENE_ENVIRONMENT");RenderSnapshot_U64("LIGHT sun","shadow_map_count",cascades);}
                sun_inputs[slots[cascade]]=map;
                SnapshotShadow(phase,"sun",cascade,base.shadow[cascade],map,true,1024,size,spans[cascade]);
                if(Pass(s,l,size,size,0,0,0,map,KICH_R4_CLEAR_DEPTH)<0)return s.failure;
                if(record)record->begun=true;
                if(Static(s,l,base.shadow[cascade],true)<0)return s.failure;
                kich_r4_lighting::Constants c=base;c.mvp=base.shadow[cascade];
                if(Models(s,l,stream,captured,ShadowModels,c)<0 || details.Draw(s,l,cb,c,true,input.engine_frame,phase,RENDER_SNAPSHOT_PASS_SUN_SHADOW)<0)return s.failure;
                if(record)record->completed=true;
                kich_r4_telemetry::Text("SHADOW",phase,"execution","PASS_COMPLETED");++shadow_passes;++frame_sun_cascades;
            }
            if(!native_mode){base.shadow[1]=base.shadow[0];sun_inputs[1]=sun_map;}
        }
        std::vector<uint8_t> shadowed;kich_r4_visibility::SelectShadowLights(input.lighting,native_mode,shadowed);
        if(PrepareLocalMaps(s,l,input.lighting,shadowed)<0)return s.failure;
        if(g_render_snapshot_active)for(unsigned i=0;i<input.lighting.count;++i){char id[64];if(input.lighting.sources[i].serial)kich_r4_telemetry::Handle(id,sizeof(id),"source",input.lighting.sources[i].serial);else snprintf(id,sizeof(id),"local_%u",i);
            kich_r4_telemetry::Text("LIGHT",id,"shadow_selection_reason",shadowed[i]?"SOURCE_REQUESTED; RETAINED_AT_POOL_RESOLUTION":"SOURCE_SHADOW_DISABLED");
            kich_r4_telemetry::U64("LIGHT",id,"shadow_maps_requested",shadowed[i]?(input.lighting.lights[i].type==2?1:6):0);}
        for(unsigned i=0;i<input.lighting.count;++i){if(!shadowed[i])continue;const KichR4LocalLight& light_source=input.lighting.lights[i];
            const uint32_t local_size=LocalSize(i);const kich_r4_lighting::Constants local=LocalConstants(light_source,local_size);current_local_size=local_size;
            const uint32_t light_sector=l.files.sector?l.files.sector(light_source.position):UINT32_MAX;
            for(unsigned face=0;face<(light_source.type==2?1u:6u);++face){
                if(!LocalFaceRendered(i,face))continue; // P0-S7: no visible receiver in this face
                char phase[48];snprintf(phase,sizeof(phase),"local_%u_face_%u",i,face);SnapshotPass("LOCAL_SHADOW",RENDER_SNAPSHOT_PASS_LOCAL_SHADOW,phase);SnapshotLocalLight(light_source,input.lighting.sources[i],i,true);
                const KichR4Handle map=LocalMap(i,face);
                kich_r4_shadow_record::Pass* record=shadow_record.Local(face,map,local_pool_generation,local_size,local.shadow[face].m,input.lighting.sources[i].serial);
                kich_r4_shadow_record::Scope record_scope(record);
                char source_id[64];if(input.lighting.sources[i].serial)kich_r4_telemetry::Handle(source_id,sizeof(source_id),"source",input.lighting.sources[i].serial);else snprintf(source_id,sizeof(source_id),"local_%u",i);
                SnapshotShadow(phase,source_id,face,local.shadow[face],map,false,light_source.range,local_size);
                if(Pass(s,l,local_size,local_size,0,0,0,map,KICH_R4_CLEAR_DEPTH)<0)return s.failure;
                if(record)record->begun=true;
                if(Static(s,l,local.shadow[face],true,light_sector)<0)return s.failure;
                kich_r4_lighting::Constants c=local;c.mvp=local.shadow[face];
                // P0-S7 r4_lamp_grass_shadows: R2 never draws grass into lamp maps.
                if(Models(s,l,stream,captured,ShadowModels,c)<0 ||
                    (input.lighting.local_detail_shadows && details.Draw(s,l,cb,c,true,input.engine_frame,phase,RENDER_SNAPSHOT_PASS_LOCAL_SHADOW)<0))return s.failure;
                shadow_record.FinishLocal();
                kich_r4_telemetry::Text("SHADOW",phase,"execution","PASS_COMPLETED");++shadow_passes;++frame_local_shadow_faces;}
            if(light_source.type==2)++frame_spot_shadow_lights;else ++frame_point_shadow_lights;}
        Mark(s,1);
        const kich_r4_scene::Matrix vp=base.mvp;
        SnapshotPass("GBUFFER",RENDER_SNAPSHOT_PASS_GBUFFER,"camera_geometry");
        if(Pass(s,l,width,height,4,albedo,normal,depth,7,NULL,light,receiver_depth)<0 || Static(s,l,vp,false)<0 || Models(s,l,stream,captured,GeometryModels,base)<0 || details.Draw(s,l,cb,base,false,input.engine_frame,"camera_geometry",RENDER_SNAPSHOT_PASS_GBUFFER)<0)return s.failure;
        if(native_mode){SnapshotPass("GBUFFER",RENDER_SNAPSHOT_PASS_GBUFFER,"wallmarks_albedo_rgb");
            if(Pass(s,l,width,height,1,albedo,0,depth,0)<0 || StaticWallmarks(s,l,vp)<0 || Models(s,l,stream,captured,Wallmarks,base)<0)return s.failure;}
        Mark(s,2);
        SnapshotPass("LIGHT_ACCUMULATION",RENDER_SNAPSHOT_PASS_LIGHT_ACCUMULATION,"sun_lighting");if(g_render_snapshot_active)RenderSnapshot_Light("sun","SCENE_ENVIRONMENT");
        sun_inputs[2]=albedo;
        // t6: R2 sunmask (alpha) for every dynamic-sun receiver, white = no mask.
        // Bound even without a mask so no sun pass samples a null view.
        {KichR4Handle mask=sun && input.lighting.sunmask_valid?l.Texture(s,"sunmask"):0;
            const bool resident=mask && mask!=l.checker && mask!=white;if(!resident)mask=white;
            if(!mask)return s.Record("P0-S1 sunmask neutral texture",KICH_R4_E_STATE);
            sun_inputs[4]=sun_mask=mask;
            if(g_render_snapshot_active){kich_r4_telemetry::Text("ENVIRONMENT","sunmask","texture",resident?"sunmask":"NEUTRAL_WHITE");
                kich_r4_telemetry::U64("ENVIRONMENT","sunmask","frame_valid",input.lighting.sunmask_valid);
                kich_r4_telemetry::Vector("ENVIRONMENT","sunmask","u",base.sunmask_u,4);kich_r4_telemetry::Vector("ENVIRONMENT","sunmask","v",base.sunmask_v,4);}}
        if(Pass(s,l,width,height,1,light,0,0,0)<0 || Fullscreen(s,l,Sun,base,normal,depth,sun_inputs,true)<0)return s.failure;
        for(unsigned i=0;i<input.lighting.count && debug!=8;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i],LocalSize(i));if(native_mode && !shadowed[i])c.alpha[3]=0;
            {char phase[48];snprintf(phase,sizeof(phase),"local_%u_lighting",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,!native_mode || shadowed[i]);}
            KichR4Handle maps[6]={};for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=native_mode && !shadowed[i]?0:LocalMap(i,face);
            LocalInputs(l,input.lighting,i,c,maps,true);
            if(Fullscreen(s,l,Local,c,normal,depth,maps,true)<0)return s.failure;
            SnapshotSubmittedLight(input.lighting.sources[i],i);++local_passes;}
        if(g_render_snapshot_active)RenderSnapshot_U64("LIGHT_REGISTRY","submitted_local_lights",input.lighting.count);
        // P0-S5 DX11 shafts (r4_sun_shafts 1/2/3 = 8/12/16 samples per segment):
        // full-size march, then a depth-aware X/Y blur; every pixel is written.
        const bool shafts_on=native_mode && !debug && sun && input.lighting.sun_shafts[0]>.5f && input.lighting.sun_shafts[1]>=.0001f;
        // Created on first use; kept through debug views and sunless frames;
        // released only when r4_sun_shafts/_intensity switch the effect off.
        const bool shafts_wanted=native_mode && input.lighting.sun_shafts[0]>.5f && input.lighting.sun_shafts[1]>=.0001f;
        if((shafts_on || !shafts_wanted) && ShaftTargets(s,l,shafts_on)<0)return s.failure;
        if(shafts_on){SnapshotPhase("sun_shafts");kich_r4_lighting::Constants c=base;c.alpha[1]=8;c.alpha[3]=0;
            memcpy(c.light_position,input.lighting.sun_shafts,sizeof(c.light_position));
            c.light_position[0]=input.lighting.sun_shafts[0]<1.5f?8.f:input.lighting.sun_shafts[0]<2.5f?12.f:16.f;
            if(Pass(s,l,width,height,1,shafts,0,0,0)<0 || Fullscreen(s,l,SunShafts,c,depth,0,sun_inputs)<0)return s.failure;
            for(unsigned vertical=0;vertical<2;++vertical){SnapshotPhase(vertical?"sun_shafts_blur_y":"sun_shafts_blur_x");
                c.light_position[0]=vertical?0.f:1.f;c.light_position[1]=vertical?1.f:0.f;
                if(Pass(s,l,width,height,1,vertical?shafts:shafts_blur,0,0,0)<0 || Fullscreen(s,l,SunShaftsBlur,c,vertical?shafts_blur:shafts,0,NULL)<0)return s.failure;}
            if(g_render_snapshot_active)kich_r4_telemetry::Vector("ENVIRONMENT","sun_shafts","mode_intensity_near_far",input.lighting.sun_shafts,4);}
        Mark(s,3);
        // t4/t5: R2 env_s0/env_s1 (sky_r_textures_env), neutral until resident.
        KichR4Handle combine_inputs[6]={normal,depth,l.CubeTexture(s,input.lighting.env_textures[0]),l.CubeTexture(s,input.lighting.env_textures[1]),0,0};
        if(s.failure<0)return s.failure;
        // Per-record telemetry: the shared summary can overflow before combine.
        if(g_render_snapshot_active){kich_r4_telemetry::Text("ENVIRONMENT","hmodel","env0",input.lighting.env_textures[0].empty()?"NONE":input.lighting.env_textures[0].c_str());
            kich_r4_telemetry::Text("ENVIRONMENT","hmodel","env1",input.lighting.env_textures[1].empty()?"NONE":input.lighting.env_textures[1].c_str());
            kich_r4_telemetry::Text("ENVIRONMENT","hmodel","env_resident",combine_inputs[2]!=l.neutral_cube && combine_inputs[3]!=l.neutral_cube?"BOTH":"NEUTRAL_FALLBACK_USED");}
        SnapshotPass("ENVIRONMENT_COMBINE",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"environment_combine");
        // R2 phase_combine renders the skybox and combine_1 into generic0/generic1 (low/high).
        const KichR4Handle exposure_maps[6]={0,0,0,0,0,native_mode?lum_pool[1]:0}; // t7: R2 s_tonemap ($user$tonemap)
        if(native_mode){combine_inputs[5]=lum_pool[1];
            if(Pass(s,l,width,height,2,scene_color,high_color,depth,KICH_R4_CLEAR_COLOR)<0 || Models(s,l,stream,captured,Environment,base,exposure_maps)<0)return s.failure;
            // Unbind DSV before sampling the completed opaque depth mask.
            SnapshotPass("COMBINE_POSTPROCESS",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"celestial_source_sky_mask");
            if(!debug && (Pass(s,l,width,height,1,scene_color,0,0,0)<0 || Models(s,l,stream,captured,Celestial,base)<0))return s.failure;
            SnapshotPass("ENVIRONMENT_COMBINE",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"environment_combine");}
        kich_r4_lighting::Constants combination=base;if(native_mode)combination.alpha[3]=20;
        if(Pass(s,l,width,height,native_mode?2:1,native_mode?scene_color:0,native_mode?high_color:0,0,0)<0 || Fullscreen(s,l,Combine,combination,albedo,light,combine_inputs)<0)return s.failure;
        // R2 phase_combine_sun_shafts: additive into generic0 (low only, not the
        // bloom source), after combine and before clouds/forward.
        if(shafts_on){SnapshotPhase("sun_shafts_composite");kich_r4_lighting::Constants c=base;c.alpha[1]=8;c.alpha[3]=0;
            if(Pass(s,l,width,height,1,scene_color,0,0,0)<0 || Fullscreen(s,l,SunShaftsComposite,c,shafts,0,NULL,true)<0)return s.failure;}
        // Match R2 phase_combine: sky/source, combine, clouds, forward. Clouds
        // must cover the source instead of having the disc blended over them.
        if(native_mode && !debug){SnapshotPhase("clouds_after_combine");
            if(Pass(s,l,width,height,1,scene_color,0,depth,0)<0 || Models(s,l,stream,captured,CloudLayer,base,exposure_maps)<0)return s.failure;}
        if(native_mode){kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;if(Pass(s,l,width,height,1,final_color,0,0,0)<0 || Fullscreen(s,l,Copy,copy,scene_color,0,NULL)<0)return s.failure;}
        Mark(s,4);
        SnapshotPass("FORWARD_TRANSPARENCY",RENDER_SNAPSHOT_PASS_FORWARD_TRANSPARENCY,"forward_base");
        if(Pass(s,l,width,height,1,native_mode?final_color:0,0,depth,0)<0)return s.failure;
        if(!debug){if(native_mode && StaticTransparent(s,l,vp)<0)return s.failure;
            if(Models(s,l,stream,captured,ForwardBase,base,sun_inputs)<0)return s.failure;
            for(unsigned i=0;i<input.lighting.count;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i],LocalSize(i));if(native_mode && !shadowed[i])c.alpha[3]=0;KichR4Handle maps[6]={};
                {char phase[48];snprintf(phase,sizeof(phase),"local_%u_forward_add",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,!native_mode || shadowed[i]);}
                for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=native_mode && !shadowed[i]?0:LocalMap(i,face);
                LocalInputs(l,input.lighting,i,c,maps);
                if(Models(s,l,stream,captured,ForwardAdd,c,maps)<0)return s.failure;}}
        if(!debug && Distort(s,l,stream,captured)<0)return s.failure;
        if(native_mode && !debug && captured && !captured->native_models.empty()){
            SnapshotPass("HUD_UI",RENDER_SNAPSHOT_PASS_HUD_UI,"hud_models");
            if(Pass(s,l,width,height,1,final_color,0,depth,KICH_R4_CLEAR_DEPTH)<0 || native_models.Draw(s,l,cb,base,*captured,KichR4ModelSession::HUD,snapshot_pass_mask,snapshot_phase,sun_inputs)<0)return s.failure;
            for(unsigned i=0;i<input.lighting.count;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i],LocalSize(i));if(!shadowed[i])c.alpha[3]=0;KichR4Handle maps[6]={};
                char phase[48];snprintf(phase,sizeof(phase),"local_%u_hud_add",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,shadowed[i]);
                for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=shadowed[i]?LocalMap(i,face):0;
                LocalInputs(l,input.lighting,i,c,maps);
                if(native_models.Draw(s,l,cb,c,*captured,KichR4ModelSession::HUDAdd,snapshot_pass_mask,snapshot_phase,maps)<0)return s.failure;}
            // P0-S4: R2 HUD lives in the G-buffer, so it occludes the sky in the
            // bloom source (generic1). The depth now holds the HUD alone.
            SnapshotPhase("hud_bloom_source");kich_r4_lighting::Constants hud=base;hud.alpha[1]=8;hud.alpha[3]=0;
            if(Pass(s,l,width,height,1,high_color,0,0,0)<0 || Fullscreen(s,l,HudBloomSource,hud,final_color,depth,NULL)<0)return s.failure;
        }
        Mark(s,5);
        if(native_mode){
            if(debug>=10){SnapshotPhase("celestial_isolated");
                if(Pass(s,l,width,height,1,final_color,0,0,KICH_R4_CLEAR_COLOR)<0 || Models(s,l,stream,captured,Celestial,base)<0)return s.failure;}
            if(debug){kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;copy.alpha[3]=0;copy.material[0]=0;
                if(Pass(s,l,width,height,1,0,0,0,0)<0 || Fullscreen(s,l,Copy,copy,final_color,0,NULL)<0)return s.failure;}
            else if(Bloom(s,l,input.lighting)<0 || Process(s,l,input)<0)return s.failure;
        }
        SnapshotPass("HUD_UI",RENDER_SNAPSHOT_PASS_HUD_UI,"hud_ui");
        if(Models(s,l,stream,captured,HUD,base)<0)return s.failure;
        Mark(s,6);Mark(s,7);
        ++frames;return 0;
    }
};
#endif
