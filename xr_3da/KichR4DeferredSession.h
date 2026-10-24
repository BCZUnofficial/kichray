#ifndef KICH_R4_DEFERRED_SESSION_H
#define KICH_R4_DEFERRED_SESSION_H
#include "KichR4Deferred.h"
#include "KichR4DeferredShaders.h"
#include "KichR4StreamSession.h"
#include "KichR4LightingMath.h"
#include "KichR4Game.h"
#include "KichR4NativeTools.h"
#include "KichR4DetailSession.h"
#include "KichR4ModelSession.h"
#include "KichR4Visibility.h"
#include "KichR4MaterialSession.h"
#include "KichR4OpticalRules.h"
#include <set>
/* EXE-side pass planner. Native resources remain owned by the R4 device;
 * destroying that device releases partial construction and all cached maps. */
struct KichR4DeferredSession {
    enum Pipeline {StaticGeometry,ModelGeometry,StaticShadow,ModelShadow,Sun,Local,ForwardSun,ForwardLocal,Plain,Combine,Sky,Clouds,Water,Distortion,StaticForward,StaticWater,Copy,Postprocess,StaticPlain,DistortionMask,DistortionComposite,CelestialPipeline,WallmarkPipeline,PipelineCount};
    KichR4DeferredAPI api;
    KichR4Handle pipelines[PipelineCount],shaders[21],cb,screen_vb,screen_ib,white;
    KichR4Handle albedo,normal,depth,light,sun_map,local_maps[8][6],scene_color,final_color,distortion_map;
    uint32_t width,height,frames,shadow_passes,local_passes;
    kich_r4_lighting::Constants base;
    bool native_mode;KichR4NativeToolsAPI tools;
    std::set<uint32_t> simple_transparent;
    KichR4DetailSession details;
    KichR4ModelSession native_models;
    uint32_t snapshot_pass_mask=0;
    char snapshot_phase[64]={};
    bool ordinary_profile=true;
    void Mark(KichR4FrameSession& s,unsigned n){if(native_mode && (ordinary_profile || g_render_snapshot_active) && tools.profile_mark)tools.profile_mark(s.device,n);}
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
        kich_r4_telemetry::Text("LIGHT",id,"attenuation","saturate(1-distance/range)^2; SPOT_MULTIPLIER=saturate((angle-cosHalf)/max(.001,(1-cosHalf)*.2))");
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
    static void SnapshotShadow(const char* id,const char* parent,unsigned face,const kich_r4_scene::Matrix& matrix,KichR4Handle target,bool sun,float range){
        if(!g_render_snapshot_active)return;
        using namespace kich_r4_telemetry;
        Text("SHADOW",id,"source_light_id",parent);U64("SHADOW",id,"face_or_cascade",face);Text("SHADOW",id,"unit",sun?"SINGLE_ORTHOGRAPHIC_SUN_MAP": "ONE_LIGHT_FACE; NOT_ONE_LAMP");
        Text("SHADOW",id,"format","DEPTH32F/D32_FLOAT");U64("SHADOW",id,"width",sun?2048:512);U64("SHADOW",id,"height",sun?2048:512);U64("SHADOW",id,"target_handle",target);
        Float("SHADOW",id,"near",sun?0.f:.03f);Float("SHADOW",id,"far",sun?1024.f:range);Vector("SHADOW",id,"view_projection_row_major",matrix.m,16);
        Text("SHADOW",id,"viewport",sun?"0,0,2048,2048,0,1":"0,0,512,512,0,1");Text("SHADOW",id,"projection",sun?"ORTHOGRAPHIC_512x512_CAMERA_CENTERED":"PERSPECTIVE_SPOT_CONE_OR_POINT_90_DEG");
        Float("SHADOW",id,"comparison_depth_bias",.0015f);Text("SHADOW",id,"comparison_bias_source","KichR4DeferredShaders PCF q.z-.0015");
        Text("SHADOW",id,"rasterizer_bias","DEFAULT_ZERO; NO_PER_LIGHT_OVERRIDE");Text("SHADOW",id,"filter","3x3 SampleCmpLevelZero; LESS_EQUAL; LINEAR_COMPARISON");
        Text("SHADOW",id,"candidate_source","LEVEL_STATIC+DETAILS+MODEL_CAMERA_SHADOW_UNION; PASS_FRUSTUM_CULL");Text("SHADOW",id,"stencil_volumes","NOT_SUPPORTED");
        Text("SHADOW",id,"execution","PLANNED");
    }
    static void SnapshotSubmittedLight(const KichR4LightIdentity& source,unsigned index){
        if(!g_render_snapshot_active)return;
        char id[64];if(source.serial)kich_r4_telemetry::Handle(id,sizeof(id),"source",source.serial);else snprintf(id,sizeof(id),"local_%u",index);
        kich_r4_telemetry::U64("LIGHT",id,"submitted",1);kich_r4_telemetry::Text("LIGHT",id,"submission_status","LOCAL_ACCUMULATION_DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED");
    }
    void SnapshotStatic(const KichR4LevelSession& l,size_t index,const char* queue){
        try{
        const kich_r4_level::Draw& mesh=l.scene.draws[index];const kich_r4_level::Material& m=l.scene.materials[mesh.material];
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
        else RenderSnapshot_Texture(stage,source && *source?source:"$white");
    }
    static void SnapshotVisibility(const char* status,const char* reason){
        RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);RenderSnapshot_Note(status,p.semantic_type[0]?p.semantic_type:"UNKNOWN",p.mesh_id[0]?p.mesh_id:"NOT_AVAILABLE",reason);
    }
    KichR4DeferredSession():cb(0),screen_vb(0),screen_ib(0),white(0),albedo(0),normal(0),depth(0),light(0),sun_map(0),scene_color(0),final_color(0),distortion_map(0),width(0),height(0),frames(0),shadow_passes(0),local_passes(0),native_mode(false){
        memset(&tools,0,sizeof(tools));
        memset(&api,0,sizeof(api));memset(pipelines,0,sizeof(pipelines));memset(shaders,0,sizeof(shaders));memset(local_maps,0,sizeof(local_maps));memset(&base,0,sizeof(base));}
    static bool ValidAPI(const KichR4DeferredAPI& a){return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_DEFERRED_ABI &&
        a.pointer_size==sizeof(void*) && !a.flags && a.create_pipeline && a.begin_pass && a.draw;}
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& level){
        if(!ValidAPI(api))return s.Record("B4.4 deferred ABI",KICH_R4_E_ARGUMENT);
        const char* entries[]={"VSStatic","VSModel","VSModelShadow","VSScreen","PSGeometry","PSShadow","PSSun","PSLocal","PSForwardSun","PSForwardLocal","PSPlain","PSCombine","PSSky","PSClouds","PSWater","PSDistortion","PSPostprocess","PSParticleDistortionMask","PSDistortionComposite","PSCelestial","PSWallmark"};
        const char* source=KichR4DeferredShaderSource();
        for(unsigned i=0;i<21;++i){level.Continue();KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=i<4?KICH_R4_VERTEX_SHADER:KICH_R4_PIXEL_SHADER;
            memcpy(d.entry,entries[i],strlen(entries[i])+1);memcpy(d.name,"kich_b44_deferred",18);
            if(level.Expect(s,entries[i],level.bridge.resources.compile_shader(s.device,&d,source,static_cast<uint32_t>(strlen(source)),&shaders[i],level.bridge.shader_error,sizeof(level.bridge.shader_error)))<0)return s.failure;}
        const unsigned vs[]={0,1,0,2,3,3,1,1,1,3,1,1,1,1,0,0,3,3,0,1,3,1,1},ps[]={4,4,5,5,6,7,8,9,10,11,12,13,14,15,8,14,10,16,10,17,18,19,20};
        for(unsigned i=0;i<PipelineCount;++i){KichR4PipelineDesc d={};d.struct_size=sizeof(d);d.vertex_shader=shaders[vs[i]];d.pixel_shader=shaders[ps[i]];
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
    int32_t Target(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle& h,uint32_t w,uint32_t n,uint32_t format,bool z){
        const KichR4TextureDesc d={sizeof(d),w,n,format,static_cast<uint32_t>(KICH_R4_SHADER_READ|(z?KICH_R4_DEPTH_TARGET:KICH_R4_COLOR_TARGET))};
        return l.Expect(s,"B4.4 target allocation",l.bridge.resources.create_texture(s.device,&d,&h));}
    int32_t ScreenTargets(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t w,uint32_t h){
        if(w==width && h==height)return 0;
        if(uint64_t(w)*h*(native_mode?40u:24u)>(native_mode?384u:192u)*1024u*1024u)return s.Record("B4.4 screen target budget",KICH_R4_E_MEMORY);
        KichR4Handle* old[]={&albedo,&normal,&depth,&light,&scene_color,&final_color,&distortion_map};
        for(unsigned i=0;i<7;++i)if(*old[i]){if(l.Expect(s,"B4.4 old screen target",l.bridge.resources.release_resource(s.device,*old[i]))<0)return s.failure;*old[i]=0;}
        if(Target(s,l,albedo,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,normal,w,h,KICH_R4_RGBA16F,false)<0 ||
            Target(s,l,depth,w,h,KICH_R4_DEPTH24_STENCIL8,true)<0 || Target(s,l,light,w,h,KICH_R4_RGBA16F,false)<0)return s.failure;
        if(native_mode && (Target(s,l,scene_color,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,final_color,w,h,KICH_R4_RGBA8,false)<0 || Target(s,l,distortion_map,w,h,KICH_R4_RGBA16F,false)<0))return s.failure;
        width=w;height=h;return 0;
    }
    int32_t Pass(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t w,uint32_t h,uint32_t count,KichR4Handle c0,KichR4Handle c1,KichR4Handle z,uint32_t clear,const float* color=NULL,KichR4Handle c2=0){
        if(g_render_snapshot_active){for(unsigned i=0;i<8;++i)RenderSnapshot_Texture(i,"HANDLE_ONLY");}
        KichR4PassDesc p={};p.struct_size=sizeof(p);p.color_count=count;p.colors[0]=c0;p.colors[1]=c1;p.colors[2]=c2;p.depth=z;p.width=w;p.height=h;p.clear_flags=clear;p.depth_value=1;if(color)memcpy(p.color,color,sizeof(p.color));
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
    static bool Opaque(const kich_r4_capture::Command& c){return c.layer==0 && !c.style.blend && c.style.depth && c.style.write && !c.effect;}
    int32_t Static(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_scene::Matrix& vp,bool shadow){
        if(!shadow)l.visible=l.culled=0;
        for(size_t i=0;i<l.scene.draws.size();++i){const kich_r4_level::Draw& mesh=l.scene.draws[i];
            const kich_r4_level::Material& m=l.scene.materials[mesh.material];const kich_r4_material::Descriptor& material=m.descriptor;
            const bool described=native_mode && material.semantic!=kich_r4_material::UNKNOWN;
            if(native_mode && (described?!(shadow?kich_r4_material::Shadow(material):kich_r4_material::Geometry(material)):m.kind>=2))continue;
            if(g_render_snapshot_active){SnapshotStatic(l,i,"R4_STATIC_OPAQUE");SnapshotVisibility("CANDIDATE_CLASSIFIED",shadow?"SHADOW_FRUSTUM":"CAMERA_FRUSTUM");}
            kich_r4_level::Draw bounds=mesh;
            if(described && mesh.tree && material.tree_wave){const float height=(std::max)(fabsf(mesh.low.y-mesh.world.m[13]),fabsf(mesh.high.y-mesh.world.m[13]));
                const float fraction=l.scene.vertices[mesh.vb].tree_fraction_max;
                const float x=fabsf(base.tree_wind[0])*height*fraction,z=fabsf(base.tree_wind[2])*height*fraction;
                bounds.low.x-=x;bounds.high.x+=x;bounds.low.z-=z;bounds.high.z+=z;}
            if(!kich_r4_level::Visible(bounds,vp)){if(!shadow)++l.culled;
                if(g_render_snapshot_active)SnapshotVisibility("CULLED",shadow?"SHADOW_FRUSTUM":"CAMERA_FRUSTUM");
                continue;}
            if(g_render_snapshot_active)SnapshotVisibility("SELECTED",shadow?"SHADOW_FRUSTUM":"CAMERA_FRUSTUM");
            kich_r4_lighting::Constants c=base;c.world=mesh.world;c.mvp=vp;
            c.normal_matrix=kich_r4_lighting::NormalMatrix(mesh.world);
            const float cutoff=native_mode?m.alpha_ref:m.cutout?200.f/255:0;c.alpha[0]=cutoff;c.alpha[1]=native_mode?float(m.alpha_func):cutoff>0?7.f:8.f;
            kich_r4_capture::Style style;style.cull=l.cull;style.depth=style.write=1;
            if(described)kich_r4_material::Apply(material,style);
            KichR4DeferredDrawDesc d=Description(pipelines[shadow?StaticShadow:StaticGeometry],l.vertex_buffers[mesh.vb],l.index_buffers[mesh.ib],cb,mesh.count,mesh.first,style);
            d.textures[0]=l.Texture(s,m.texture);
            if(described){kich_r4_material::Bind(s,l,material,mesh.available_attributes,c,d,shadow,true,mesh.tree?(!shadow?112u:0u)|(material.tree_wave?128u:0u):0u);
                if(mesh.tree && !shadow){
                    for(unsigned a=0;a<4;++a){c.color_scale[a]=mesh.tree_scale[a]*base.tree_wind[3];c.color_bias[a]=mesh.tree_bias[a]*base.tree_wind[3];}
                    c.sun_scale_bias[0]=mesh.tree_scale[4]*base.tree_wind[3];c.sun_scale_bias[1]=mesh.tree_bias[4]*base.tree_wind[3];}}
            if(native_mode && !shadow && m.kind==1){
                // Existing landscape layer convention retains its dedicated slots.
                c.material[0]=0;c.alpha[1]=8;c.alpha[3]=10;d.textures[1]=0;
                d.textures[2]=l.Texture(s,m.texture+"_mask");for(unsigned j=0;j<4;++j)d.textures[j+3]=l.Texture(s,m.layers[j]);}
            if(Constants(s,l,c)<0)return s.failure;
            SnapshotResolvedTexture(l,0,m.texture.c_str(),d.textures[0]);
            if(l.Expect(s,"G3c static material draw",api.draw(s.device,&d))<0)return s.failure;
            if(!shadow){++l.visible;++l.submitted_draws;l.submitted_triangles+=mesh.count/3;}
        }if(!shadow)++l.frames;return 0;
    }
    int32_t StaticTransparent(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_scene::Matrix& vp){
        std::vector<size_t> order;for(size_t i=0;i<l.scene.draws.size();++i)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
            const kich_r4_level::Draw& x=l.scene.draws[a];const kich_r4_level::Draw& y=l.scene.draws[b];
            const float ax=(x.low.x+x.high.x)*.5f-base.camera[0],ay=(x.low.y+x.high.y)*.5f-base.camera[1],az=(x.low.z+x.high.z)*.5f-base.camera[2];
            const float bx=(y.low.x+y.high.x)*.5f-base.camera[0],by=(y.low.y+y.high.y)*.5f-base.camera[1],bz=(y.low.z+y.high.z)*.5f-base.camera[2];
            return ax*ax+ay*ay+az*az>bx*bx+by*by+bz*bz;});
        for(size_t sorted=0;sorted<order.size();++sorted){const size_t i=order[sorted];const kich_r4_level::Draw& mesh=l.scene.draws[i];const kich_r4_level::Material& m=l.scene.materials[mesh.material];
            const bool described=m.descriptor.semantic!=kich_r4_material::UNKNOWN;
            if(described?!(m.descriptor.passes&kich_r4_material::PASS_FORWARD):m.kind<2)continue;
            if(g_render_snapshot_active){SnapshotStatic(l,i,"R4_STATIC_TRANSPARENT");SnapshotVisibility("CANDIDATE_CLASSIFIED","CAMERA_FRUSTUM");}
            if(!kich_r4_level::Visible(mesh,vp)){if(g_render_snapshot_active)SnapshotVisibility("CULLED","CAMERA_FRUSTUM");continue;}
            if(g_render_snapshot_active)SnapshotVisibility("SELECTED","CAMERA_FRUSTUM");
            kich_r4_lighting::Constants c=base;c.world=mesh.world;c.mvp=vp;c.normal_matrix=kich_r4_lighting::NormalMatrix(mesh.world);c.alpha[0]=m.alpha_ref;c.alpha[1]=static_cast<float>(m.alpha_func);
            if(Constants(s,l,c)<0)return s.failure;
            kich_r4_capture::Style style;style.blend=m.blend;style.src=m.source;style.dst=m.destination;style.write=0;style.cull=KICH_R4_CULL_NONE;if(described){kich_r4_material::Apply(m.descriptor,style);c.alpha[0]=m.descriptor.alpha_ref;c.alpha[1]=float(m.descriptor.alpha_func);if(Constants(s,l,c)<0)return s.failure;}
            KichR4DeferredDrawDesc d=Description(pipelines[m.kind==2?StaticWater:StaticForward],l.vertex_buffers[mesh.vb],l.index_buffers[mesh.ib],cb,mesh.count,mesh.first,style);d.textures[0]=l.Texture(s,m.texture);d.textures[1]=scene_color;d.textures[2]=sun_map;
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
    int32_t Models(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const kich_r4_capture::Batch* batch,
        ModelPass pass,const kich_r4_lighting::Constants& frame,const KichR4Handle* maps=NULL){
        if(!batch)return 0;
        KichR4ModelSession::Pass model_pass=KichR4ModelSession::PassCount;
        if(pass==GeometryModels)model_pass=KichR4ModelSession::Geometry;
        else if(pass==ShadowModels)model_pass=KichR4ModelSession::Shadow;
        else if(pass==ForwardBase)model_pass=KichR4ModelSession::Forward;
        else if(pass==ForwardAdd)model_pass=KichR4ModelSession::ForwardAdd;
        else if(pass==DistortionModels)model_pass=KichR4ModelSession::Distortion;
        const bool mixed=native_mode && (pass==ForwardBase || pass==ForwardAdd || pass==DistortionModels);
        const size_t stream_count=batch->commands.size();
        std::vector<size_t> order(stream_count+(mixed?batch->native_models.size():0));for(size_t i=0;i<order.size();++i)order[i]=i;
        if(mixed)std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
            auto distance=[&](size_t index)->double{
                if(index>=stream_count)return KichR4ModelSession::Distance(batch->native_models[index-stream_count],frame);
                const kich_r4_capture::Command& c=batch->commands[index];
                if(!c.count || c.first>=batch->indices.size())return 0;
                const uint32_t v=batch->indices[c.first];if(v>=batch->vertices.size())return 0;
                double d=0;for(unsigned j=0;j<3;++j){const double delta=double(batch->vertices[v].world[j])-frame.camera[j];d+=delta*delta;}return d;};
            return distance(a)>distance(b);});
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
            if(pass==ShadowModels && !Opaque(c))continue;
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
            kich_r4_lighting::Constants constants=frame;constants.alpha[0]=c.style.alpha_ref;constants.alpha[1]=static_cast<float>(c.style.alpha_func);constants.alpha[2]=c.style.blend?1.f:0.f;
            if(native_mode && (c.effect==2 || c.effect==3 || c.effect==1 || c.effect==5 || c.effect==7 || pass==Celestial))constants.alpha[3]=0;
            if(native_mode && pass==MenuPP)constants.alpha[3]=30;
            if(pass==Wallmarks)constants.alpha[3]=static_cast<float>(c.wallmark_color);
            if(Constants(s,l,constants)<0)return s.failure;
            kich_r4_capture::Style style=c.style;
            if(pass==Wallmarks){style.depth=1;style.write=0;style.surface.color_mask=7;KichR4ReceiverState(style.surface,false);}
            if(pass==Celestial){kich_r4_optics::SkyStyle(style);if(frame.screen[2]==12)style.blend=0;}
            if(native_mode && pass==MenuPP)style.blend=0;
            unsigned pipeline=pass==GeometryModels?ModelGeometry:pass==ShadowModels?ModelShadow:pass==ForwardBase?ForwardSun:pass==ForwardAdd?ForwardLocal:Plain;
            if(native_mode && pass!=ShadowModels && pass!=GeometryModels){if(c.effect==2)pipeline=Sky;else if(c.effect==3)pipeline=Clouds;else if(c.effect==4)pipeline=Water;else if(c.effect==5 || c.effect==7)pipeline=Distortion;else if(c.effect==1)pipeline=Plain;}
            if(pass==Celestial)pipeline=CelestialPipeline;
            if(pass==Wallmarks)pipeline=WallmarkPipeline;
            if(pass==DistortionModels){pipeline=DistortionMask;style.depth=1;style.write=0;style.blend=1;style.src=5;style.dst=6;}
            if(pass==ShadowModels){style.depth=style.write=1;style.blend=style.clamp=0;style.scissor=false;style.min_z=0;style.max_z=1;}
            if(pass==ForwardAdd){style.blend=1;style.src=style.dst=2;style.write=0;}
            KichR4DeferredDrawDesc d=Description(pipelines[pipeline],stream.vb,stream.ib,cb,c.count,c.first,style);
            d.textures[0]=c.texture.empty()?white:l.Texture(s,c.texture);
            if(pass==Celestial)d.textures[1]=depth;
            if(!native_mode && pass==HUD && d.textures[0]==l.checker)return s.Record("B4.4 HUD DDS unavailable",KICH_R4_E_UNSUPPORTED);
            if(maps)for(unsigned j=0;j<6;++j)d.textures[j+2]=maps[j];
            if(native_mode && c.effect){if(c.effect==2 || c.effect==3)d.textures[1]=c.secondary.empty()?d.textures[0]:l.Texture(s,c.secondary);
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
        if(add){style.blend=1;style.src=style.dst=2;}
        KichR4DeferredDrawDesc d=Description(pipelines[p],screen_vb,screen_ib,cb,3,0,style);d.textures[0]=t0;d.textures[1]=t1;
        if(maps)for(unsigned i=0;i<6;++i)d.textures[i+2]=maps[i];
        const int32_t result=l.Expect(s,"B4.4 fullscreen draw",api.draw(s.device,&d));
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;char id[96];snprintf(id,sizeof(id),"%s_pipeline_%u",snapshot_phase,static_cast<unsigned>(p));
            Text("POSTPROCESS",id,"execution",result>=0?"DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED":"DRAW_FAILED");
            U64("POSTPROCESS",id,"pipeline",p);U64("POSTPROCESS",id,"pipeline_handle",pipelines[p]);U64("POSTPROCESS",id,"input0_handle",t0);U64("POSTPROCESS",id,"input1_handle",t1);
            Text("POSTPROCESS",id,"output","SEE_BACKEND_PASS_RENDER_TARGET_HANDLES");U64("POSTPROCESS",id,"additive",add?1:0);}
        return result;
    }
    int32_t DrawMenu(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const KichR4GameFrame& input,const kich_r4_capture::Batch& batch){
        SnapshotCPU(&batch);native_models.ResetFrame();details.ResetFrame(input.engine_frame);SnapshotPass("MENU_UI",RENDER_SNAPSHOT_PASS_MENU_UI,"menu");
        base.screen[0]=static_cast<float>(input.width);base.screen[1]=static_cast<float>(input.height);
        if(!cb && Start(s,l)<0)return s.failure;
        if(ScreenTargets(s,l,input.width,input.height)<0 || stream.Upload(s,l,batch)<0)return s.failure;
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
        memcpy(c.alpha,p,16);memcpy(c.light_position,p+4,16);c.light_direction[0]=p[9];c.light_direction[1]=p[8];
        memcpy(c.sun_color,p+10,12);memcpy(c.ambient,p+13,12);memcpy(c.hemi,p+16,12);memcpy(c.fog,p+19,12);
        if(menu){c.alpha[3]=0;memset(c.light_position,0,sizeof(c.light_position));
            for(unsigned i=0;i<3;++i){c.sun_color[i]=.5f;c.ambient[i]=0;c.hemi[i]=1.f/3.f;}}
        if(g_render_snapshot_active){using namespace kich_r4_telemetry;const char* id="native_postprocess";
            Text("POSTPROCESS",id,"requested_layout","gamma,brightness,contrast,gray,blur,duality_h,duality_v,noise,noise_phase,noise_scale,baseRGB,addRGB,grayRGB,balanceRGB");
            Vector("POSTPROCESS",id,"requested_0_15",p,16);Vector("POSTPROCESS",id,"requested_16_21",p+16,6);
            Vector("POSTPROCESS",id,"applied_gamma_brightness_contrast_gray",c.alpha,4);Vector("POSTPROCESS",id,"applied_blur_duality_noise",c.light_position,4);
            Vector("POSTPROCESS",id,"applied_noise_scale_phase",c.light_direction,2);Vector("POSTPROCESS",id,"applied_baseRGB",c.sun_color,3);Vector("POSTPROCESS",id,"applied_addRGB",c.ambient,3);Vector("POSTPROCESS",id,"applied_grayRGB",c.hemi,3);Vector("POSTPROCESS",id,"applied_balanceRGB",c.fog,3);
            Text("POSTPROCESS",id,"override",menu?"MENU_ZERO_GRAY_BLUR_DUALITY_NOISE; BASE=.5 ADD=0 GRAY=1/3":"NONE");
            Text("POSTPROCESS",id,"shader_parameter_transform","gamma=max(.01,alpha.x); brightness=alpha.y*.5; contrast=alpha.z*.5; SATURATED_OUTPUT");
            Text("POSTPROCESS",id,"HDR_exposure_bloom","NOT_SUPPORTED_BY_NATIVE_POSTPROCESS");Text("POSTPROCESS",id,"output_color_format","SWAPCHAIN_UNORM; NO_HDR_TONEMAP");
            U64("POSTPROCESS",id,"input_handle",final_color);Text("POSTPROCESS",id,"output","SWAPCHAIN_BACKBUFFER");Text("POSTPROCESS",id,"execution","PLANNED");}
        if(Pass(s,l,input.width,input.height,1,0,0,0,0)<0)return s.failure;
        const int32_t result=Fullscreen(s,l,Postprocess,c,final_color,0,NULL);
        kich_r4_telemetry::Text("POSTPROCESS","native_postprocess","execution",result>=0?"DRAW_SUCCEEDED; PIXEL_CONTRIBUTION_NOT_MEASURED":"DRAW_FAILED");return result;
    }
    kich_r4_lighting::Constants LocalConstants(const KichR4LocalLight& l)const {
        kich_r4_lighting::Constants c=base;c.alpha[3]=1;memcpy(c.light_position,l.position,12);c.light_position[3]=l.range;
        const kich_r4_scene::Vec3 direction=kich_r4_scene::Normalize(kich_r4_scene::Vec3{l.direction[0],l.direction[1],l.direction[2]});
        c.light_direction[0]=direction.x;c.light_direction[1]=direction.y;c.light_direction[2]=direction.z;c.light_direction[3]=cosf(l.cone*.5f);
        memcpy(c.light_color,l.color,12);c.light_color[3]=static_cast<float>(l.type);c.screen[3]=512;
        for(unsigned i=0;i<(l.type==2?1u:6u);++i)c.shadow[i]=kich_r4_lighting::LocalShadow(l,i);
        return c;
    }
    double model_prepare_ms=0;
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4StreamSession& stream,const KichR4GameFrame& input,const kich_r4_capture::Batch* captured,uint32_t debug){
        if(!native_mode)l.texture_registry.Begin(); // Native presenter owns its submission epoch.
        ordinary_profile=debug==0; // diagnostic GPU timings must not enter the normal profile ring
        SnapshotCPU(captured);native_models.ResetFrame();details.ResetFrame(input.engine_frame);
        if(g_render_snapshot_active){RenderSnapshot_U64("LIGHT_REGISTRY","selected_frame_inputs",input.lighting.count);RenderSnapshot_Text("ENVIRONMENT","source","COPIED_CURRENT_ENV_LIGHTING_FRAME");
            kich_r4_telemetry::Vector("ENVIRONMENT","weather","sun_direction",input.lighting.sun_direction,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","sun_color",input.lighting.sun_color,3);
            kich_r4_telemetry::Vector("ENVIRONMENT","weather","ambient",input.lighting.ambient,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","hemi",input.lighting.hemi,3);kich_r4_telemetry::Vector("ENVIRONMENT","weather","fog",input.lighting.fog,3);
            RenderSnapshot_Text("ENVIRONMENT","sky_cloud_execution","NO_SUCCESSFUL_DRAW_RECORDED_YET");RenderSnapshot_Text("ENVIRONMENT","weather_texture_names","SEE_ENVIRONMENT_COMMAND_TEXTURE_RECORDS; NOT_DERIVED_FROM_LIGHTING_VALUES");
            RenderSnapshot_Text("ENVIRONMENT","fog_applied","FOG_RGB_AS_EMPTY_DEPTH_BACKGROUND_IN_LEGACY_COMBINE; NATIVE_SKY_PRESERVED; NO_ATMOSPHERIC_DISTANCE_FOG");
            RenderSnapshot_Text("ENVIRONMENT","ambient_hemi_applied","SunLight: ambient+hemi*saturate(normal.y*.5+.5)*vertex_hemi");
            RenderSnapshot_Text("ENVIRONMENT","sun_disk_direction_link","NOT_INSTRUMENTED; LIGHT_DIRECTION_AND_SHADOW_PROJECTION_RECORDED_SEPARATELY");
            RenderSnapshot_U64("TRANSPARENCY","debug_mode",debug);RenderSnapshot_Text("TRANSPARENCY","debug_view",kich_r4_optics::DebugView(debug));RenderSnapshot_Text("TRANSPARENCY","forward_requested",!debug?"ENABLED":"DISABLED_BY_DEBUG_VIEW");
            RenderSnapshot_Text("TRANSPARENCY","sorting","STABLE_BACK_TO_FRONT_DISTANCE; SHARED_STREAM_AND_NATIVE_MODEL_QUEUE");RenderSnapshot_Text("TRANSPARENCY","distortion_requested",native_mode && HasDistortion(captured)?"HAS_COMMANDS":"NO_COMMANDS_OR_UNSUPPORTED");}
        Mark(s,0);
        if(!input.lighting.valid || input.lighting.count>8)return s.Record("B4.4 weather snapshot",KICH_R4_E_ARGUMENT);
        const float* weather[]={input.lighting.sun_direction,input.lighting.sun_color,input.lighting.ambient,input.lighting.hemi,input.lighting.fog};
        for(unsigned i=0;i<5;++i)for(unsigned j=0;j<3;++j)if(!kich_r4_lighting::Finite(weather[i][j]))return s.Record("B4.4 weather values",KICH_R4_E_ARGUMENT);
        for(unsigned i=0;i<3;++i)if(!kich_r4_lighting::Finite(input.position[i]))return s.Record("B4.4 camera position",KICH_R4_E_ARGUMENT);
        for(unsigned i=0;i<input.lighting.count;++i)if(!kich_r4_lighting::ValidLight(input.lighting.lights[i]))return s.Record("B4.4 local light",KICH_R4_E_ARGUMENT);
        if(!cb && Start(s,l)<0)return s.failure;
        if(ScreenTargets(s,l,input.width,input.height)<0)return s.failure;
        if(native_mode && details.source){
            if(details.Start(s,l,KichR4DeferredShaderSource(),shaders[4],shaders[5])<0 || details.Upload(s,l,input.engine_frame)<0)return s.failure;}
        if(captured && stream.Upload(s,l,*captured)<0)return s.failure;
        const auto model_started=kich_r4_metrics::Clock::now();
        if(captured && PrepareModels(s,l,*captured)<0)return s.failure;
        model_prepare_ms=kich_r4_metrics::Elapsed(model_started);
        memset(&base,0,sizeof(base));base.world=base.normal_matrix=kich_r4_scene::Identity();memcpy(base.mvp.m,input.view_projection,64);
        if(!kich_r4_lighting::Inverse(base.mvp,base.inverse_vp))return s.Record("B4.4 inverse camera",KICH_R4_E_ARGUMENT);
        memcpy(base.tree_wave,input.tree_wave,16);memcpy(base.tree_wind,input.tree_wind,16);base.tree_wind[3]=input.tree_sbc;
        memcpy(base.camera,input.position,12);base.camera[3]=150;base.light_position[3]=input.time;base.screen[0]=static_cast<float>(width);base.screen[1]=static_cast<float>(height);base.screen[2]=static_cast<float>(debug);base.screen[3]=2048;
        const kich_r4_scene::Vec3 dir=kich_r4_scene::Normalize(kich_r4_scene::Vec3{weather[0][0],weather[0][1],weather[0][2]});
        base.sun_direction[0]=dir.x;base.sun_direction[1]=dir.y;base.sun_direction[2]=dir.z;
        memcpy(base.sun_color,weather[1],12);memcpy(base.ambient,weather[2],12);memcpy(base.hemi,weather[3],12);memcpy(base.fog,weather[4],12);
        const bool sun=kich_r4_scene::Dot(dir,dir)>.5f && (base.sun_color[0]>0 || base.sun_color[1]>0 || base.sun_color[2]>0);
        base.sun_color[3]=sun?1.f:0.f;
        KichR4Handle sun_inputs[6]={};
        if(sun){if(!sun_map && Target(s,l,sun_map,2048,2048,KICH_R4_DEPTH32F,true)<0)return s.failure;
            SnapshotPass("SUN_SHADOW",RENDER_SNAPSHOT_PASS_SUN_SHADOW,"sun_0");
            if(g_render_snapshot_active){RenderSnapshot_Light("sun","SCENE_ENVIRONMENT");RenderSnapshot_Text("LIGHT sun","id_scope","SCENE_ENVIRONMENT");RenderSnapshot_Text("LIGHT sun","type","DIRECTIONAL");
                char value[160];snprintf(value,sizeof(value),"%.9g,%.9g,%.9g",base.sun_direction[0],base.sun_direction[1],base.sun_direction[2]);RenderSnapshot_Text("LIGHT sun","direction",value);
                snprintf(value,sizeof(value),"%.9g,%.9g,%.9g",base.sun_color[0],base.sun_color[1],base.sun_color[2]);RenderSnapshot_Text("LIGHT sun","color",value);RenderSnapshot_U64("LIGHT sun","shadow_map_count",1);}
            base.shadow[0]=kich_r4_lighting::SunShadow(input.position,base.sun_direction);sun_inputs[0]=sun_map;
            SnapshotShadow("sun_0","sun",0,base.shadow[0],sun_map,true,1024);
            if(Pass(s,l,2048,2048,0,0,0,sun_map,KICH_R4_CLEAR_DEPTH)<0 || Static(s,l,base.shadow[0],true)<0)return s.failure;
            kich_r4_lighting::Constants c=base;c.mvp=base.shadow[0];
            if(Models(s,l,stream,captured,ShadowModels,c)<0 || details.Draw(s,l,cb,c,true,input.engine_frame,"sun_0",RENDER_SNAPSHOT_PASS_SUN_SHADOW)<0)return s.failure;
            kich_r4_telemetry::Text("SHADOW","sun_0","execution","PASS_COMPLETED");++shadow_passes;}
        bool shadowed[8]={};kich_r4_visibility::SelectShadowLights(input.lighting,native_mode,shadowed);
        if(g_render_snapshot_active)for(unsigned i=0;i<input.lighting.count;++i){char id[64];if(input.lighting.sources[i].serial)kich_r4_telemetry::Handle(id,sizeof(id),"source",input.lighting.sources[i].serial);else snprintf(id,sizeof(id),"local_%u",i);
            kich_r4_telemetry::Text("LIGHT",id,"shadow_selection_reason",!native_mode?"LEGACY_BRIDGE_ALL_SELECTED_LIGHTS":shadowed[i]?"NATIVE_SPOT_OR_POINT_BUDGET_SELECTED":!(input.lighting.shadow_mask&(1u<<i))?"SOURCE_SHADOW_DISABLED":"NATIVE_SHADOW_BUDGET_LIMIT");
            kich_r4_telemetry::U64("LIGHT",id,"shadow_maps_requested",(!native_mode || shadowed[i])?(input.lighting.lights[i].type==2?1:6):0);}
        for(unsigned i=0;i<input.lighting.count;++i){if(native_mode && !shadowed[i])continue;const KichR4LocalLight& light_source=input.lighting.lights[i];const kich_r4_lighting::Constants local=LocalConstants(light_source);
            for(unsigned face=0;face<(light_source.type==2?1u:6u);++face){
                char phase[48];snprintf(phase,sizeof(phase),"local_%u_face_%u",i,face);SnapshotPass("LOCAL_SHADOW",RENDER_SNAPSHOT_PASS_LOCAL_SHADOW,phase);SnapshotLocalLight(light_source,input.lighting.sources[i],i,true);
                if(!local_maps[i][face] && Target(s,l,local_maps[i][face],512,512,KICH_R4_DEPTH32F,true)<0)return s.failure;
                char source_id[64];if(input.lighting.sources[i].serial)kich_r4_telemetry::Handle(source_id,sizeof(source_id),"source",input.lighting.sources[i].serial);else snprintf(source_id,sizeof(source_id),"local_%u",i);
                SnapshotShadow(phase,source_id,face,local.shadow[face],local_maps[i][face],false,light_source.range);
                if(Pass(s,l,512,512,0,0,0,local_maps[i][face],KICH_R4_CLEAR_DEPTH)<0 || Static(s,l,local.shadow[face],true)<0)return s.failure;
                kich_r4_lighting::Constants c=local;c.mvp=local.shadow[face];
                if(Models(s,l,stream,captured,ShadowModels,c)<0 || details.Draw(s,l,cb,c,true,input.engine_frame,phase,RENDER_SNAPSHOT_PASS_LOCAL_SHADOW)<0)return s.failure;
                kich_r4_telemetry::Text("SHADOW",phase,"execution","PASS_COMPLETED");++shadow_passes;}}
        Mark(s,1);
        const kich_r4_scene::Matrix vp=base.mvp;
        SnapshotPass("GBUFFER",RENDER_SNAPSHOT_PASS_GBUFFER,"camera_geometry");
        if(Pass(s,l,width,height,3,albedo,normal,depth,7,NULL,light)<0 || Static(s,l,vp,false)<0 || Models(s,l,stream,captured,GeometryModels,base)<0 || details.Draw(s,l,cb,base,false,input.engine_frame,"camera_geometry",RENDER_SNAPSHOT_PASS_GBUFFER)<0)return s.failure;
        if(native_mode){SnapshotPass("GBUFFER",RENDER_SNAPSHOT_PASS_GBUFFER,"wallmarks_albedo_rgb");
            if(Pass(s,l,width,height,1,albedo,0,depth,0)<0 || Models(s,l,stream,captured,Wallmarks,base)<0)return s.failure;}
        Mark(s,2);
        SnapshotPass("LIGHT_ACCUMULATION",RENDER_SNAPSHOT_PASS_LIGHT_ACCUMULATION,"sun_lighting");if(g_render_snapshot_active)RenderSnapshot_Light("sun","SCENE_ENVIRONMENT");
        sun_inputs[1]=albedo;
        if(Pass(s,l,width,height,1,light,0,0,0)<0 || Fullscreen(s,l,Sun,base,normal,depth,sun_inputs,true)<0)return s.failure;
        for(unsigned i=0;i<input.lighting.count && debug!=8;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i]);if(native_mode && !shadowed[i])c.alpha[3]=0;
            {char phase[48];snprintf(phase,sizeof(phase),"local_%u_lighting",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,!native_mode || shadowed[i]);}
            KichR4Handle maps[6]={};for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=native_mode && !shadowed[i]?0:local_maps[i][face];
            if(Fullscreen(s,l,Local,c,normal,depth,maps,true)<0)return s.failure;
            SnapshotSubmittedLight(input.lighting.sources[i],i);++local_passes;}
        if(g_render_snapshot_active)RenderSnapshot_U64("LIGHT_REGISTRY","submitted_local_lights",input.lighting.count);
        Mark(s,3);
        KichR4Handle combine_inputs[6]={normal,depth,0,0,0,0};
        SnapshotPass("ENVIRONMENT_COMBINE",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"environment_combine");
        if(native_mode){if(Pass(s,l,width,height,1,scene_color,0,depth,KICH_R4_CLEAR_COLOR)<0 || Models(s,l,stream,captured,Environment,base)<0)return s.failure;
            // Unbind DSV before sampling the completed opaque depth mask.
            SnapshotPass("COMBINE_POSTPROCESS",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"celestial_source_sky_mask");
            if(!debug && (Pass(s,l,width,height,1,scene_color,0,0,0)<0 || Models(s,l,stream,captured,Celestial,base)<0))return s.failure;
            SnapshotPass("ENVIRONMENT_COMBINE",RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE,"environment_combine");}
        kich_r4_lighting::Constants combination=base;if(native_mode)combination.alpha[3]=20;
        if(Pass(s,l,width,height,1,native_mode?scene_color:0,0,0,0)<0 || Fullscreen(s,l,Combine,combination,albedo,light,combine_inputs)<0)return s.failure;
        // Match R2 phase_combine: sky/source, combine, clouds, forward. Clouds
        // must cover the source instead of having the disc blended over them.
        if(native_mode && !debug){SnapshotPhase("clouds_after_combine");
            if(Pass(s,l,width,height,1,scene_color,0,depth,0)<0 || Models(s,l,stream,captured,CloudLayer,base)<0)return s.failure;}
        if(native_mode){kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;if(Pass(s,l,width,height,1,final_color,0,0,0)<0 || Fullscreen(s,l,Copy,copy,scene_color,0,NULL)<0)return s.failure;}
        Mark(s,4);
        SnapshotPass("FORWARD_TRANSPARENCY",RENDER_SNAPSHOT_PASS_FORWARD_TRANSPARENCY,"forward_base");
        if(Pass(s,l,width,height,1,native_mode?final_color:0,0,depth,0)<0)return s.failure;
        if(!debug){if(native_mode && StaticTransparent(s,l,vp)<0)return s.failure;
            if(Models(s,l,stream,captured,ForwardBase,base,sun_inputs)<0)return s.failure;
            for(unsigned i=0;i<input.lighting.count;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i]);if(native_mode && !shadowed[i])c.alpha[3]=0;KichR4Handle maps[6]={};
                {char phase[48];snprintf(phase,sizeof(phase),"local_%u_forward_add",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,!native_mode || shadowed[i]);}
                for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=native_mode && !shadowed[i]?0:local_maps[i][face];
                if(Models(s,l,stream,captured,ForwardAdd,c,maps)<0)return s.failure;}}
        if(!debug && Distort(s,l,stream,captured)<0)return s.failure;
        if(native_mode && !debug && captured && !captured->native_models.empty()){
            SnapshotPass("HUD_UI",RENDER_SNAPSHOT_PASS_HUD_UI,"hud_models");
            if(Pass(s,l,width,height,1,final_color,0,depth,KICH_R4_CLEAR_DEPTH)<0 || native_models.Draw(s,l,cb,base,*captured,KichR4ModelSession::HUD,snapshot_pass_mask,snapshot_phase,sun_inputs)<0)return s.failure;
            for(unsigned i=0;i<input.lighting.count;++i){kich_r4_lighting::Constants c=LocalConstants(input.lighting.lights[i]);if(!shadowed[i])c.alpha[3]=0;KichR4Handle maps[6]={};
                char phase[48];snprintf(phase,sizeof(phase),"local_%u_hud_add",i);SnapshotPhase(phase);SnapshotLocalLight(input.lighting.lights[i],input.lighting.sources[i],i,shadowed[i]);
                for(unsigned face=0;face<(input.lighting.lights[i].type==2?1u:6u);++face)maps[face]=shadowed[i]?local_maps[i][face]:0;
                if(native_models.Draw(s,l,cb,c,*captured,KichR4ModelSession::HUDAdd,snapshot_pass_mask,snapshot_phase,maps)<0)return s.failure;}
        }
        Mark(s,5);
        if(native_mode){
            if(debug>=10){SnapshotPhase("celestial_isolated");
                if(Pass(s,l,width,height,1,final_color,0,0,KICH_R4_CLEAR_COLOR)<0 || Models(s,l,stream,captured,Celestial,base)<0)return s.failure;}
            if(debug){kich_r4_lighting::Constants copy=base;copy.alpha[1]=8;copy.alpha[3]=0;copy.material[0]=0;
                if(Pass(s,l,width,height,1,0,0,0,0)<0 || Fullscreen(s,l,Copy,copy,final_color,0,NULL)<0)return s.failure;}
            else if(Process(s,l,input)<0)return s.failure;
        }
        SnapshotPass("HUD_UI",RENDER_SNAPSHOT_PASS_HUD_UI,"hud_ui");
        if(Models(s,l,stream,captured,HUD,base)<0)return s.failure;
        Mark(s,6);Mark(s,7);
        ++frames;return 0;
    }
};
#endif
