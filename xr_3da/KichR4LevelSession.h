#ifndef KICH_R4_LEVEL_SESSION_H
#define KICH_R4_LEVEL_SESSION_H
#include "KichR4GeometrySession.h"
#include "KichR4Level.h"
#include "KichR4Dds.h"
#include "KichR4LevelCamera.h"
#include "KichR4FrameMetrics.h"
#include <map>
#include <stdio.h>
#include <stdarg.h>
struct KichR4LevelSession {
    KichR4GeometrySession bridge;KichR4LevelAPI api;KichR4LevelFiles files;
    kich_r4_level::Scene scene;kich_r4_level::Camera camera;
    std::vector<KichR4Handle> vertex_buffers,index_buffers,materials;
    std::map<std::string,KichR4Handle> textures;
    KichR4Handle white,checker;
    uint32_t textures_loaded,texture_fallbacks,budget_skips,frames,visible,culled,camera_steps,revision,cull,depth;
    uint64_t texture_bytes,submitted_triangles,submitted_draws;
    double texture_io_ms=0,texture_parse_ms=0,texture_create_ms=0;
    bool textured,cancelled;
    std::string detail;
    void (*log)(const char*);
    KichR4Handle (*texture_override)(void*,const std::string&,bool&);void* texture_context;
    bool (*keep_going)(void*);void* progress_context;
    KichR4LevelSession():white(0),checker(0),textures_loaded(0),texture_fallbacks(0),budget_skips(0),frames(0),visible(0),culled(0),camera_steps(0),revision(0),cull(KICH_R4_CULL_BACK),depth(KICH_R4_DEPTH_ON),texture_bytes(0),submitted_triangles(0),submitted_draws(0),textured(true),cancelled(false),log(NULL),texture_override(NULL),texture_context(NULL),keep_going(NULL),progress_context(NULL){
        memset(&bridge,0,sizeof(bridge));memset(&api,0,sizeof(api));memset(&files,0,sizeof(files));}
    static bool ValidAPI(const KichR4LevelAPI& a){return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_LEVEL_ABI &&
        a.pointer_size==sizeof(void*) && !a.flags && a.create_texture_dds && a.create_pipeline && a.draw_textured;}
    void Write(const char* format,...){if(!log)return;char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);text[sizeof(text)-1]=0;log(text);}
    struct Cancel {};
    void Continue(){if(keep_going && !keep_going(progress_context))throw Cancel();}
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
    KichR4Handle Texture(KichR4FrameSession& s,const std::string& name){
        if(name.empty())return white;
        if(texture_override){bool handled=false;const KichR4Handle h=texture_override(texture_context,name,handled);if(handled)return h;}
        const std::map<std::string,KichR4Handle>::const_iterator cached=textures.find(name);if(cached!=textures.end())return cached->second;
        KichR4Handle result=checker;kich_r4_level::File file(files);std::string path=name;
        if(path.size()<4 || kich_r4_level::Lower(path.substr(path.size()-4))!=".dds")path+=".dds";
        int32_t opened;auto measured=kich_r4_metrics::Clock::now();
        if(texture_override){
            const bool absolute=name.find(':')!=std::string::npos || name[0]=='\\' || name[0]=='/';
            if(absolute)opened=file.Open("$r4_absolute$",path.c_str());
            else{opened=file.Open("$level$",path.c_str());if(opened!=0)opened=file.Open("$game_saves$",path.c_str());if(opened!=0)opened=file.Open("$game_textures$",path.c_str());}
        }else opened=file.Open("$game_textures$",path.c_str());
        texture_io_ms+=kich_r4_metrics::Elapsed(measured);measured=kich_r4_metrics::Clock::now();
        kich_r4_dds::Image image={};int32_t hr=opened;
        const bool parsed=opened==0 && kich_r4_dds::Parse(file.view.data,file.view.bytes,2048,image);
        texture_parse_ms+=kich_r4_metrics::Elapsed(measured);
        if(parsed){
            const uint64_t payload=kich_r4_dds::UploadBytes(image);
            if(payload<=384u*1024u*1024u-texture_bytes){const KichR4DdsDesc d={sizeof(d),2048,0};
                measured=kich_r4_metrics::Clock::now();
                KichR4Handle loaded=0;hr=api.create_texture_dds(s.device,&d,file.view.data,file.view.bytes,&loaded);
                texture_create_ms+=kich_r4_metrics::Elapsed(measured);
                if(hr==0 && loaded){result=loaded;++textures_loaded;texture_bytes+=payload;}
                else if(loaded)bridge.resources.release_resource(s.device,loaded);
            }else{++budget_skips;hr=KICH_R4_E_MEMORY;}
        }else if(opened==0)hr=KICH_R4_E_UNSUPPORTED;
        if(result==checker){++texture_fallbacks;if(texture_fallbacks<=24)Write("[R4 bridge B4.2] texture fallback: %s; HRESULT=0x%08X",path.c_str(),static_cast<uint32_t>(hr));}
        textures[name]=result;return result;
    }
    int32_t Start(KichR4FrameSession& s,const char* name){try{
        const auto started=kich_r4_metrics::Clock::now();auto measured=started;double io_ms=0,parse_ms=0;
        Continue();kich_r4_level::Check(files.open && files.close,"engine filesystem adapter unavailable");
        kich_r4_level::Check(name && kich_r4_level::SafeName(name) && strlen(name)<64 && !strchr(name,'\\') && !strchr(name,'/'),"invalid level name");
        Write("[R4 bridge B4.2] loading: $game_levels$/%s/level + level.geom",name);
        {
            kich_r4_level::File level(files),geom(files);const std::string prefix=std::string(name)+"\\";
            if(Expect(s,"B4.2 open level",level.Open("$game_levels$",(prefix+"level").c_str()))<0)return s.failure;
            if(Expect(s,"B4.2 open level.geom",geom.Open("$game_levels$",(prefix+"level.geom").c_str()))<0)return s.failure;
            io_ms=kich_r4_metrics::Elapsed(measured);measured=kich_r4_metrics::Clock::now();
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
        if(Placeholder(s,false,&white)<0 || Placeholder(s,true,&checker)<0)return s.failure;
        measured=kich_r4_metrics::Clock::now();
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
        for(size_t i=0;i<materials.size();++i)if(used[i]){Continue();materials[i]=Texture(s,scene.materials[i].texture);}
        Write("[R4 bridge B4.2] ready: VBs=%u IBs=%u; DDS loaded=%u fallback=%u budget_skips=%u; texture payload=%u MiB; base color / alpha cutout",
            static_cast<uint32_t>(vertex_buffers.size()),static_cast<uint32_t>(index_buffers.size()),textures_loaded,texture_fallbacks,budget_skips,static_cast<uint32_t>(texture_bytes/(1024u*1024u)));
        if(texture_override)Write("[R4 G3e load] level_io_ms=%.3f parse_basis_ms=%.3f geometry_create_ms=%.3f level_setup_ms=%.3f; texture phases reported after material resolution; CPU wall times, not GPU durations",io_ms,parse_ms,buffers_ms,kich_r4_metrics::Elapsed(started));
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
        if(!w || !h)return s.Record("B4.2 zero viewport",KICH_R4_E_ARGUMENT);
        visible=culled=0;kich_r4_scene::Matrix previous={};float previous_cutoff=-1;
        for(size_t i=0;i<scene.draws.size();++i){const kich_r4_level::Draw& mesh=scene.draws[i];if(!kich_r4_level::Visible(mesh,vp)){++culled;continue;}
            const float cutoff=textured && scene.materials[mesh.material].cutout?200.f/255.f:0.f;
            if(!visible || memcmp(&previous,&mesh.world,sizeof(previous)) || cutoff!=previous_cutoff){kich_r4_scene::Constants constants={};
                constants.mvp=kich_r4_scene::Multiply(mesh.world,vp);constants.tint[0]=constants.tint[1]=constants.tint[2]=1;constants.tint[3]=cutoff;
                if(Expect(s,"B4.2 update constants",bridge.api.update_buffer(s.device,bridge.cb,&constants,sizeof(constants)))<0)return s.failure;
                previous=mesh.world;previous_cutoff=cutoff;}
            KichR4TexturedDrawDesc d={};d.struct_size=sizeof(d);d.draw={sizeof(d.draw),bridge.pipeline,vertex_buffers[mesh.vb],index_buffers[mesh.ib],bridge.cb,mesh.count,mesh.first,cull,depth};
            d.texture=textured?materials[mesh.material]:white;
            if(Expect(s,"B4.2 DrawIndexed textured",api.draw_textured(s.device,&d))<0)return s.failure;
            ++visible;++submitted_draws;submitted_triangles+=mesh.count/3;
        }++frames;return KICH_R4_OK;
    }catch(const std::exception& e){detail=e.what();return s.Record("B4.2 draw scene",KICH_R4_E_STATE);}}
};
#endif
