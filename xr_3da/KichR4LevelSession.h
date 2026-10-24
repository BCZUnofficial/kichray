#ifndef KICH_R4_LEVEL_SESSION_H
#define KICH_R4_LEVEL_SESSION_H
#include "KichR4GeometrySession.h"
#include "KichR4Level.h"
#include "KichR4Dds.h"
#include "KichR4MaterialLut.h"
#include "KichR4LevelCamera.h"
#include "KichR4FrameMetrics.h"
#include "KichR4TexturePolicy.h"
#include <map>
#include <stdio.h>
#include <stdarg.h>
struct KichR4LevelSession {
    KichR4GeometrySession bridge;KichR4LevelAPI api;KichR4LevelFiles files;
    kich_r4_level::Scene scene;kich_r4_level::Camera camera;
    std::vector<KichR4Handle> vertex_buffers,index_buffers,materials;
    std::map<std::string,KichR4Handle> textures;
    kich_r4_texture::Registry texture_registry;
    // Keep a bounded part of cold preflight input until its planned upload.
    // Readers still close in NeedTexture; no filesystem lease crosses a draw.
    std::map<std::string,std::vector<unsigned char> > prepared_textures;
    uint64_t prepared_texture_bytes=0,prepared_texture_epoch=0;
    KichR4Handle white,checker,neutral_cube,material_lut=0;
    uint32_t textures_loaded,texture_fallbacks,budget_skips,frames,visible,culled,camera_steps,revision,cull,depth;
    uint64_t texture_bytes,submitted_triangles,submitted_draws;
    double texture_io_ms=0,texture_parse_ms=0,texture_create_ms=0;
    // Preflight probes reuse actual residency work; no diagnostic traversal.
    // traversal/commit are inclusive scopes. planning/io/parse/upload are
    // nested attribution and must not be added to their parent times.
    struct PreflightProbe {
        bool measured=false;
        double traversal_ms=0,planning_ms=0,commit_ms=0,io_ms=0,parse_ms=0,upload_ms=0;
        uint64_t opens=0,open_failures=0,source_bytes=0,prepared_hits=0,prepared_bytes=0;
        uint64_t metadata_hits=0,retry_skips=0,resident_hits=0,uploads=0,upload_bytes=0,upload_failures=0,evictions=0;
    } preflight_probe;
    PreflightProbe* texture_probe=NULL;
    bool texture_measure_snapshot=false;
    struct PreflightProbeScope {
        KichR4LevelSession& level;PreflightProbe* previous;bool snapshot_before;uint64_t evictions_before;
        PreflightProbeScope(KichR4LevelSession& owner,bool snapshot):level(owner),previous(owner.texture_probe),snapshot_before(owner.texture_measure_snapshot),evictions_before(owner.texture_registry.evictions){
            level.preflight_probe=PreflightProbe();level.preflight_probe.measured=kich_r4_record::Measure(snapshot);
            level.texture_probe=level.preflight_probe.measured?&level.preflight_probe:NULL;level.texture_measure_snapshot=snapshot;
        }
        ~PreflightProbeScope(){if(level.preflight_probe.measured)level.preflight_probe.evictions=level.texture_registry.evictions-evictions_before;
            level.texture_probe=previous;level.texture_measure_snapshot=snapshot_before;}
    };
    bool textured,cancelled;
    std::string detail;
    void (*log)(const char*);
    KichR4Handle (*texture_override)(void*,const std::string&,bool&);void* texture_context;
    bool (*keep_going)(void*);void* progress_context;
    KichR4LevelSession():white(0),checker(0),neutral_cube(0),textures_loaded(0),texture_fallbacks(0),budget_skips(0),frames(0),visible(0),culled(0),camera_steps(0),revision(0),cull(KICH_R4_CULL_BACK),depth(KICH_R4_DEPTH_ON),texture_bytes(0),submitted_triangles(0),submitted_draws(0),textured(true),cancelled(false),log(NULL),texture_override(NULL),texture_context(NULL),keep_going(NULL),progress_context(NULL){
        memset(&bridge,0,sizeof(bridge));memset(&api,0,sizeof(api));files=KichR4LevelFiles();}
    static bool ValidAPI(const KichR4LevelAPI& a){return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_LEVEL_ABI &&
        a.pointer_size==sizeof(void*) && !a.flags && a.create_texture_dds && a.create_pipeline && a.draw_textured;}
    void Write(const char* format,...){if(!log)return;char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);text[sizeof(text)-1]=0;log(text);}
    struct Cancel {};
    void Continue(){if(keep_going && !keep_going(progress_context))throw Cancel();}
    uint64_t PreparedTextureLimit()const{return (std::min)(UINT64_C(32)*1024*1024,texture_registry.budget/8);}
    void ClearPreparedTextures(){prepared_textures.clear();prepared_texture_bytes=0;prepared_texture_epoch=texture_registry.epoch;}
    void SyncPreparedTextures(){if(prepared_texture_epoch!=texture_registry.epoch || prepared_texture_bytes>PreparedTextureLimit())ClearPreparedTextures();}
    struct PreparedScope {
        KichR4LevelSession& level;
        explicit PreparedScope(KichR4LevelSession& owner):level(owner){level.SyncPreparedTextures();}
        ~PreparedScope(){level.ClearPreparedTextures();}
    };
    void PrepareTexture(const std::string& key,const KichR4FileView& view){
        SyncPreparedTextures();
        if(prepared_textures.size()>=128 || view.bytes>PreparedTextureLimit()-prepared_texture_bytes || prepared_textures.count(key))return;
        // This cache is optional: allocation pressure keeps the established
        // reopen-at-upload path, rather than failing a valid texture request.
        try{std::vector<unsigned char> bytes(view.data,view.data+view.bytes);
            prepared_textures.emplace(key,std::move(bytes));prepared_texture_bytes+=view.bytes;
        }catch(const std::bad_alloc&){}
    }
    void TakePreparedTexture(const std::string& key,std::vector<unsigned char>& bytes){
        SyncPreparedTextures();const auto found=prepared_textures.find(key);if(found==prepared_textures.end())return;
        bytes.swap(found->second);prepared_texture_bytes-=bytes.size();prepared_textures.erase(found);
        if(texture_probe){++texture_probe->prepared_hits;texture_probe->prepared_bytes+=bytes.size();}
    }
    int32_t Expect(KichR4FrameSession& s,const char* step,int32_t hr){return bridge.Expect(s,step,hr);}
    int32_t Compile(KichR4FrameSession& s,uint32_t kind,const char* source,KichR4Handle* out){
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=kind;memcpy(d.entry,"main",5);memcpy(d.name,"kich_b42_level",15);
        return Expect(s,kind==KICH_R4_VERTEX_SHADER?"B4.2 compile vs_5_0":"B4.2 compile ps_5_0",bridge.resources.compile_shader(s.device,&d,source,static_cast<uint32_t>(strlen(source)),out,bridge.shader_error,sizeof(bridge.shader_error)));}
    static void Put32(std::vector<unsigned char>& d,unsigned offset,uint32_t value){for(unsigned i=0;i<4;++i)d[offset+i]=static_cast<unsigned char>(value>>(i*8));}
    int32_t Placeholder(KichR4FrameSession& s,bool grid,KichR4Handle* out){std::vector<unsigned char> d(128+8*8*4,0);
        Put32(d,0,0x20534444);Put32(d,4,124);Put32(d,8,0x100f);Put32(d,12,8);Put32(d,16,8);Put32(d,20,32);
        Put32(d,76,32);Put32(d,80,0x41);Put32(d,88,32);Put32(d,92,0xff);Put32(d,96,0xff00);Put32(d,100,0xff0000);Put32(d,104,0xff000000);Put32(d,108,0x1000);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const unsigned at=128+(y*8+x)*4;const bool dark=((x/2+y/2)%2)!=0;
            d[at]=grid?(dark?32:255):210;d[at+1]=grid?32:210;d[at+2]=grid?(dark?32:255):210;d[at+3]=255;}
        const KichR4DdsDesc desc={sizeof(desc),2048,0};return Expect(s,"B4.2 fallback texture",api.create_texture_dds(s.device,&desc,d.data(),static_cast<uint32_t>(d.size()),out));}
    KichR4Handle NeutralCube(KichR4FrameSession& s){
        if(neutral_cube)return neutral_cube;
        // A TextureCube shader cannot sample the ordinary 2D white/checker.
        // One neutral pixel per face is persistent and outside the DDS LRU.
        std::vector<unsigned char> d(128+6*4,0);
        Put32(d,0,0x20534444);Put32(d,4,124);Put32(d,8,0x100f);Put32(d,12,1);Put32(d,16,1);Put32(d,20,4);
        Put32(d,76,32);Put32(d,80,0x41);Put32(d,88,32);Put32(d,92,0xff);Put32(d,96,0xff00);Put32(d,100,0xff0000);Put32(d,104,0xff000000);
        Put32(d,108,0x1008);Put32(d,112,0xfe00);
        for(unsigned i=0;i<6;++i){d[128+i*4]=d[129+i*4]=d[130+i*4]=32;d[131+i*4]=255;}
        const KichR4DdsDesc desc={sizeof(desc),1,0};KichR4Handle loaded=0;
        const int32_t hr=api.create_texture_dds(s.device,&desc,d.data(),static_cast<uint32_t>(d.size()),&loaded);
        if(hr==0 && loaded)neutral_cube=loaded;
        else{if(loaded)bridge.resources.release_resource(s.device,loaded);if(kich_r4_texture::FatalDevice(hr))s.Record("E1.2 neutral cube device failure",hr);}
        return neutral_cube;
    }
    // P0-L3 R2 s_material, persistent and outside the DDS LRU like the neutral
    // cube. Lighting cannot fall back to another table: a failure fails the frame.
    KichR4Handle MaterialLut(KichR4FrameSession& s){
        if(material_lut)return material_lut;
        const std::vector<unsigned char> d=kich_r4_material_lut::Dds();
        const KichR4DdsDesc desc={sizeof(desc),kich_r4_material_lut::Height,0};KichR4Handle loaded=0;
        const int32_t hr=api.create_texture_dds(s.device,&desc,d.data(),static_cast<uint32_t>(d.size()),&loaded);
        if(hr==0 && loaded)material_lut=loaded;
        else{if(loaded)bridge.resources.release_resource(s.device,loaded);s.Record("P0-L3 material LUT texture",hr<0?hr:KICH_R4_E_ARGUMENT);}
        return material_lut;
    }
    KichR4Handle CubeTexture(KichR4FrameSession& s,const std::string& name){
        if(!name.empty()){
            NeedTexture(name,kich_r4_texture::EFFECT);
            const auto found=texture_registry.entries.find(kich_r4_texture::Key(name));
            if(found!=texture_registry.entries.end() && found->second.faces==6){const KichR4Handle h=Texture(s,name);
                if(h && h!=checker && h!=white)return h;}
        }
        if(s.failure<0)return 0;
        return NeutralCube(s);
    }
    int32_t OpenTexture(kich_r4_level::File& file,const std::string& name){
        std::string path=name;if(path.size()<4 || kich_r4_level::Lower(path.substr(path.size()-4))!=".dds")path+=".dds";
        const auto started=kich_r4_metrics::Start(texture_measure_snapshot);int32_t opened;
        if(texture_override){const bool absolute=name.find(':')!=std::string::npos || name[0]=='\\' || name[0]=='/';
            if(absolute)opened=file.Open("$r4_absolute$",path.c_str());
            else{opened=file.Open("$level$",path.c_str());if(opened!=0)opened=file.Open("$game_saves$",path.c_str());if(opened!=0)opened=file.Open("$game_textures$",path.c_str());}}
        else opened=file.Open("$game_textures$",path.c_str());
        const double elapsed=kich_r4_metrics::Elapsed(started);texture_io_ms+=elapsed;
        if(texture_probe){texture_probe->io_ms+=elapsed;++texture_probe->opens;
            if(opened)++texture_probe->open_failures;else texture_probe->source_bytes+=file.view.bytes;}
        return opened;
    }
    static void TextureLevels(kich_r4_texture::Entry& entry,const kich_r4_dds::Image& image){
        entry.faces=image.faces;entry.levels.clear();uint64_t remaining=kich_r4_dds::UploadBytes(image);
        uint32_t w=image.width,h=image.height;
        for(uint32_t i=0;i<image.count;++i){
            kich_r4_texture::Mip option={i?(std::max)(w,h):16384u,w,h,image.count-i,remaining};
            entry.levels.push_back(option);
            remaining-=image.format>=kich_r4_dds::BC1?uint64_t(image.mips[i].bytes)*image.faces:uint64_t(w)*h*4*image.faces;
            w=w>1?w/2:1;h=h>1?h/2:1;}
    }
    void NeedTexture(const std::string& name,uint32_t role){
        SyncPreparedTextures();
        if(name.empty())return;
        // Runtime uploads have their own lifetime and cannot be evicted by DDS
        // residency. Resolve only at the actual draw/PrepareUI boundary.
        if(name.compare(0,12,"r4_runtime__")==0)return;
        const std::string key=kich_r4_texture::Key(name);
        kich_r4_texture::Entry& e=texture_registry.entries[key];
        if(e.source.empty())e.source=name;
        e.role=e.wanted==texture_registry.epoch?(std::min)(e.role,role):role;e.wanted=texture_registry.epoch;
        if(!e.levels.empty() || e.retry>texture_registry.epoch){if(texture_probe){
            if(!e.levels.empty())++texture_probe->metadata_hits;else ++texture_probe->retry_skips;}return;}
        kich_r4_level::File file(files);const int32_t opened=OpenTexture(file,e.source);
        const auto started=kich_r4_metrics::Start(texture_measure_snapshot);kich_r4_dds::Image image={};
        const bool parsed=opened==0 && kich_r4_dds::Parse(file.view.data,file.view.bytes,16384,image);
        if(parsed){TextureLevels(e,image);PrepareTexture(key,file.view);}
        else{e.reason=opened?kich_r4_texture::SourceUnavailable:kich_r4_texture::InvalidLayout;e.failure=opened?opened:KICH_R4_E_UNSUPPORTED;e.retry=texture_registry.epoch+300;}
        const double elapsed=kich_r4_metrics::Elapsed(started);texture_parse_ms+=elapsed;if(texture_probe)texture_probe->parse_ms+=elapsed;
    }
    int32_t CommitTexturePlan(KichR4FrameSession& s,bool pump_loading=false){
        PreparedScope prepared(*this);try{
        const auto planning_started=kich_r4_metrics::Start(texture_measure_snapshot);
        texture_registry.Plan();uint64_t required=0;
        auto release=[&](uint64_t h){const int32_t hr=bridge.resources.release_resource(s.device,h);if(hr<0)s.Record("E1.2 texture residency release",hr);return hr;};
        std::vector<kich_r4_texture::Entry*> work;
        for(auto i=texture_registry.entries.begin();i!=texture_registry.entries.end();++i){kich_r4_texture::Entry& e=i->second;
            if(e.planned!=texture_registry.epoch)continue;
            if(e.choice==UINT32_MAX){e.reason=kich_r4_texture::BudgetPressure;continue;}
            if(e.handle && e.resident!=e.choice){if(release(e.handle)<0)return s.failure;
                texture_registry.bytes-=e.bytes;e.handle=e.bytes=0;++texture_registry.evictions;}
            // Reserve every retained dependency before admitting any new one.
            e.touched=texture_registry.epoch;if(!e.handle)required+=e.levels[e.choice].bytes;
            e.retry=0;work.push_back(&e);
        }
        if(!texture_registry.MakeRoom(required,release))return s.failure<0?s.failure:s.Record("E1.2 planned texture capacity",KICH_R4_E_MEMORY);
        std::stable_sort(work.begin(),work.end(),[](const kich_r4_texture::Entry* a,const kich_r4_texture::Entry* b){return a->role<b->role;});
        const double planning_elapsed=kich_r4_metrics::Elapsed(planning_started);if(texture_probe)texture_probe->planning_ms+=planning_elapsed;
        for(size_t i=0;i<work.size();++i){if(pump_loading && !work[i]->handle)Continue();Texture(s,work[i]->source);if(s.failure<0)return s.failure;}
        texture_bytes=texture_registry.bytes;return KICH_R4_OK;
        }catch(const Cancel&){cancelled=true;return KICH_R4_FRAME_SUSPENDED;}
    }
    KichR4Handle Texture(KichR4FrameSession& s,const std::string& name,bool retry_budget=false){
        SyncPreparedTextures();
        if(name.empty())return white;
        if(texture_override){bool handled=false;const KichR4Handle h=texture_override(texture_context,name,handled);if(handled)return h;}
        const std::string key=kich_r4_texture::Key(name);
        kich_r4_texture::Entry& entry=texture_registry.entries[key];
        entry.touched=texture_registry.epoch;
        if(entry.source.empty())entry.source=name;
        if(entry.planned==texture_registry.epoch && entry.choice==UINT32_MAX)return checker;
        if(entry.handle){if(texture_probe)++texture_probe->resident_hits;return entry.handle;}
        // UI admission precedes world submission and may now evict yesterday's
        // world textures. Do not keep a previous late-frame budget failure on
        // cooldown when this ordered admission explicitly requests a retry.
        if(retry_budget && entry.reason==kich_r4_texture::BudgetPressure)entry.retry=0;
        if(entry.retry>texture_registry.epoch){if(texture_probe)++texture_probe->retry_skips;return checker;}
        KichR4Handle result=checker;kich_r4_level::File file(files);std::vector<unsigned char> prepared;
        TakePreparedTexture(key,prepared);
        const int32_t opened=prepared.empty()?OpenTexture(file,name):KICH_R4_OK;auto measured=kich_r4_metrics::Start(texture_measure_snapshot);
        const unsigned char* data=prepared.empty()?file.view.data:prepared.data();
        const uint32_t bytes=prepared.empty()?file.view.bytes:static_cast<uint32_t>(prepared.size());
        kich_r4_dds::Image image={};int32_t hr=opened;
        const bool parsed=opened==0 && kich_r4_dds::Parse(data,bytes,16384,image);
        entry.reason=opened!=0?kich_r4_texture::SourceUnavailable:!parsed?kich_r4_texture::InvalidLayout:kich_r4_texture::UploadFailed;
        const double parse_elapsed=kich_r4_metrics::Elapsed(measured);texture_parse_ms+=parse_elapsed;if(texture_probe)texture_probe->parse_ms+=parse_elapsed;
        if(parsed){
            if(entry.levels.empty())TextureLevels(entry,image);
            uint32_t dimension=entry.planned==texture_registry.epoch?entry.levels[entry.choice].dimension:16384;
            if(dimension!=16384){kich_r4_dds::Image smaller={};if(kich_r4_dds::Parse(data,bytes,dimension,smaller))image=smaller;}
            uint64_t payload=kich_r4_dds::UploadBytes(image);
            auto release=[&](uint64_t h){const int32_t released=bridge.resources.release_resource(s.device,h);if(kich_r4_texture::FatalDevice(released))s.Record("E1 texture eviction device failure",released);return released;};
            bool room=texture_registry.MakeRoom(payload,release);
            // Under pressure use only existing mip levels. A no-mip texture
            // remains valid and retries later; it is never declared malformed.
            while(!room && image.count>1 && entry.planned!=texture_registry.epoch){dimension=(image.width>image.height?image.width:image.height)/2;
                kich_r4_dds::Image smaller={};if(!kich_r4_dds::Parse(data,bytes,dimension,smaller))break;
                image=smaller;payload=kich_r4_dds::UploadBytes(image);room=texture_registry.MakeRoom(payload,release);}
            if(room){const KichR4DdsDesc d={sizeof(d),dimension,0};
                measured=kich_r4_metrics::Start(texture_measure_snapshot);
                KichR4Handle loaded=0;hr=api.create_texture_dds(s.device,&d,data,bytes,&loaded);
                const double upload_elapsed=kich_r4_metrics::Elapsed(measured);texture_create_ms+=upload_elapsed;
                if(texture_probe){texture_probe->upload_ms+=upload_elapsed;
                    if(hr==0 && loaded){++texture_probe->uploads;texture_probe->upload_bytes+=payload;}else ++texture_probe->upload_failures;}
                if(hr==0 && loaded){result=loaded;++textures_loaded;entry.handle=loaded;entry.bytes=payload;texture_registry.bytes+=payload;entry.reason=kich_r4_texture::Ready;entry.width=image.width;entry.height=image.height;entry.mips=image.count;
                    for(size_t i=0;i<entry.levels.size();++i)if(entry.levels[i].width==image.width && entry.levels[i].height==image.height){entry.resident=static_cast<uint32_t>(i);break;}}
                else if(loaded)bridge.resources.release_resource(s.device,loaded);
            }else{++budget_skips;hr=KICH_R4_E_MEMORY;entry.reason=kich_r4_texture::BudgetPressure;}
        }else if(opened==0)hr=KICH_R4_E_UNSUPPORTED;
        if(result==checker){++texture_fallbacks;if(texture_fallbacks<=24)Write("[R4 bridge B4.2] texture fallback: %s; HRESULT=0x%08X",name.c_str(),static_cast<uint32_t>(hr));}
        texture_bytes=texture_registry.bytes;
        if(kich_r4_texture::FatalDevice(hr))s.Record("E1 texture device failure",hr);
        entry.failure=result==checker?hr:0;
        entry.retry=result==checker?texture_registry.epoch+(opened!=0 || !parsed?300:2):0;
        // This map is diagnostic only; the registry owns resident handles.
        textures[name]=result;return result;
    }
    int32_t Start(KichR4FrameSession& s,const char* name){try{
        ClearPreparedTextures();
        const auto started=kich_r4_metrics::Start(texture_measure_snapshot);auto measured=started;double io_ms=0,parse_ms=0;
        Continue();kich_r4_level::Check(files.open && files.close,"engine filesystem adapter unavailable");
        kich_r4_level::Check(name && kich_r4_level::SafeName(name) && strlen(name)<64 && !strchr(name,'\\') && !strchr(name,'/'),"invalid level name");
        Write("[R4 bridge B4.2] loading: $game_levels$/%s/level + level.geom",name);
        {
            kich_r4_level::File level(files),geom(files);const std::string prefix=std::string(name)+"\\";
            if(Expect(s,"B4.2 open level",level.Open("$game_levels$",(prefix+"level").c_str()))<0)return s.failure;
            if(Expect(s,"B4.2 open level.geom",geom.Open("$game_levels$",(prefix+"level.geom").c_str()))<0)return s.failure;
            io_ms=kich_r4_metrics::Elapsed(measured);measured=kich_r4_metrics::Start(texture_measure_snapshot);
            kich_r4_level::Parse(level.Data(),geom.Data(),scene);parse_ms=kich_r4_metrics::Elapsed(measured);
        }
        Write("[R4 bridge B4.2] level=%s; visuals=%u static_meshes=%u trees=%u skipped_nodes=%u triangles=%u; CPU geometry=%u MiB",
            name,scene.visuals,static_cast<uint32_t>(scene.draws.size()),scene.trees,scene.skipped,scene.triangles,static_cast<uint32_t>(scene.cpu_bytes/(1024u*1024u)));
        camera.Reset(scene.low,scene.high);
        if(Compile(s,KICH_R4_VERTEX_SHADER,kich_r4_level::VertexShader(),&bridge.vs)<0 || Compile(s,KICH_R4_PIXEL_SHADER,kich_r4_level::PixelShader(),&bridge.ps)<0)return s.failure;
        kich_r4_scene::Constants initial={};initial.mvp=kich_r4_scene::Identity();initial.tint[0]=initial.tint[1]=initial.tint[2]=1;
        KichR4BufferDesc b={sizeof(b),KICH_R4_CONSTANT_BUFFER,sizeof(initial),0};
        if(Expect(s,"B4.2 constant buffer",bridge.resources.create_buffer(s.device,&b,&initial,sizeof(initial),&bridge.cb))<0)return s.failure;
        KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=sizeof(kich_r4_level::Vertex);p.element_count=2;p.vertex_shader=bridge.vs;p.pixel_shader=bridge.ps;
        p.elements[0]={KICH_R4_POSITION,0,KICH_R4_FLOAT3,0};p.elements[1]={KICH_R4_TEXCOORD,0,KICH_R4_FLOAT2,12};
        if(Expect(s,"B4.2 textured pipeline",api.create_pipeline(s.device,&p,&bridge.pipeline))<0)return s.failure;
        if((!white && Placeholder(s,false,&white)<0) || (!checker && Placeholder(s,true,&checker)<0))return s.failure;
        measured=kich_r4_metrics::Start(texture_measure_snapshot);
        for(size_t i=0;i<scene.vertices.size();++i){Continue();std::vector<kich_r4_level::Vertex>& data=scene.vertices[i].vertices;KichR4Handle handle=0;
            b.kind=KICH_R4_VERTEX_BUFFER;b.stride=sizeof(data[0]);b.byte_size=static_cast<uint32_t>(data.size()*sizeof(data[0]));
            if(Expect(s,"B4.2 vertex buffer",bridge.resources.create_buffer(s.device,&b,data.data(),b.byte_size,&handle))<0)return s.failure;
            vertex_buffers.push_back(handle);std::vector<kich_r4_level::Vertex>().swap(data);}
        for(size_t i=0;i<scene.indices.size();++i){Continue();std::vector<uint32_t>& data=scene.indices[i];KichR4Handle handle=0;
            b.kind=KICH_R4_INDEX_BUFFER;b.stride=4;b.byte_size=static_cast<uint32_t>(data.size()*4);
            if(Expect(s,"B4.2 index buffer",bridge.resources.create_buffer(s.device,&b,data.data(),b.byte_size,&handle))<0)return s.failure;
            index_buffers.push_back(handle);std::vector<uint32_t>().swap(data);}
        const double buffers_ms=kich_r4_metrics::Elapsed(measured);
        materials.resize(scene.materials.size(),white);std::vector<bool> used(materials.size(),false);
        for(size_t i=0;i<scene.draws.size();++i)used[scene.draws[i].material]=true;
        for(size_t i=0;i<materials.size();++i)if(!texture_override && used[i]){Continue();materials[i]=Texture(s,scene.materials[i].texture);}
        Write("[R4 bridge B4.2] ready: VBs=%u IBs=%u; DDS loaded=%u fallback=%u budget_skips=%u; texture payload=%u MiB; base color / alpha cutout",
            static_cast<uint32_t>(vertex_buffers.size()),static_cast<uint32_t>(index_buffers.size()),textures_loaded,texture_fallbacks,budget_skips,static_cast<uint32_t>(texture_bytes/(1024u*1024u)));
        if(texture_override && started.measured)Write("[R4 G3e load] level_io_ms=%.3f parse_basis_ms=%.3f geometry_create_ms=%.3f level_setup_ms=%.3f; texture phases reported after material resolution; CPU wall times, not GPU durations",io_ms,parse_ms,buffers_ms,kich_r4_metrics::Elapsed(started));
        return KICH_R4_OK;
    }catch(const Cancel&){cancelled=true;return KICH_R4_FRAME_SUSPENDED;}
     catch(const std::bad_alloc&){detail="viewer allocation failed";return s.Record("B4.2 load level",KICH_R4_E_MEMORY);}
     catch(const std::exception& e){detail=e.what();return s.Record("B4.2 parse/load level",KICH_R4_E_ARGUMENT);}}
    void Key(unsigned key){if(key=='T')textured=!textured;else if(key=='C')cull=(cull+1)%3;else if(key=='Z')depth=1-depth;
        else if(key==0x24)camera.Reset(scene.low,scene.high);
        else return;
        ++revision;}
    int32_t Draw(KichR4FrameSession& s,uint32_t w,uint32_t h){
        if(!w || !h)return s.Record("B4.2 zero viewport",KICH_R4_E_ARGUMENT);
        return DrawWithMatrix(s,w,h,camera.ViewProjection(w,h));
    }
    int32_t DrawWithMatrix(KichR4FrameSession& s,uint32_t w,uint32_t h,const kich_r4_scene::Matrix& vp){try{
        texture_registry.Begin();
        if(!w || !h)return s.Record("B4.2 zero viewport",KICH_R4_E_ARGUMENT);
        visible=culled=0;kich_r4_scene::Matrix previous={};float previous_cutoff=-1;
        for(size_t i=0;i<scene.draws.size();++i){const kich_r4_level::Draw& mesh=scene.draws[i];if(!kich_r4_level::Visible(mesh,vp)){++culled;continue;}
            const float cutoff=textured && scene.materials[mesh.material].cutout?200.f/255.f:0.f;
            if(!visible || memcmp(&previous,&mesh.world,sizeof(previous)) || cutoff!=previous_cutoff){kich_r4_scene::Constants constants={};
                constants.mvp=kich_r4_scene::Multiply(mesh.world,vp);constants.tint[0]=constants.tint[1]=constants.tint[2]=1;constants.tint[3]=cutoff;
                if(Expect(s,"B4.2 update constants",bridge.api.update_buffer(s.device,bridge.cb,&constants,sizeof(constants)))<0)return s.failure;
                previous=mesh.world;previous_cutoff=cutoff;}
            KichR4TexturedDrawDesc d={};d.struct_size=sizeof(d);d.draw={sizeof(d.draw),bridge.pipeline,vertex_buffers[mesh.vb],index_buffers[mesh.ib],bridge.cb,mesh.count,mesh.first,cull,depth};
            d.texture=textured?Texture(s,scene.materials[mesh.material].texture):white;
            if(Expect(s,"B4.2 DrawIndexed textured",api.draw_textured(s.device,&d))<0)return s.failure;
            ++visible;++submitted_draws;submitted_triangles+=mesh.count/3;
        }++frames;return KICH_R4_OK;
    }catch(const std::exception& e){detail=e.what();return s.Record("B4.2 draw scene",KICH_R4_E_STATE);}}
};
#endif
