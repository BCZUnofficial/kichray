#ifndef KICH_R4_DEFERRED_TEST_RECORDING_H
#define KICH_R4_DEFERRED_TEST_RECORDING_H
#include "../r4_level/recording.h"
#include "../../xr_3da/KichR4GameSession.h"
#include "../../xr_3da/xrRender_R4/R4DeferredRules.h"
namespace {
std::map<KichR4Handle,KichR4TextureDesc> targets;
std::map<KichR4Handle,KichR4PipelineDesc> native_pipelines;
std::map<KichR4Handle,std::string> shader_entries;
std::vector<KichR4PassDesc> passes;
std::vector<std::string> draw_entries;
std::vector<kich_r4_lighting::Constants> draw_constants;
KichR4PassDesc active_pass={};bool pass_active=false;
void ResetDeferredRecorder(){targets.clear();native_pipelines.clear();shader_entries.clear();passes.clear();draw_entries.clear();draw_constants.clear();pass_active=false;}
int32_t DCompile(KichR4Handle,const KichR4ShaderDesc* d,const char* source,uint32_t bytes,KichR4Handle* out,char*,uint32_t){*out=0;
    const int32_t hr=f->Call("deferred shader");if(hr)return hr;
    Require(d->struct_size==sizeof(*d) && source && strlen(source)==bytes && d->entry[0],"complete shader source and explicit SM5 entry");
    *out=f->Add(d->kind);shader_entries[*out]=d->entry;return 0;}
int32_t DBuffer(KichR4Handle,const KichR4BufferDesc* d,const void* data,uint32_t bytes,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call("deferred buffer");if(hr)return hr;
    Require(ValidBuffer(*d,data,bytes),"deferred buffers satisfy B2 contract");
    *out=f->Add(d->kind,data,bytes,d->stride);if(!data)f->Find(*out).bytes.resize(d->byte_size);return 0;}
int32_t DTexture(KichR4Handle,const KichR4TextureDesc* d,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call("deferred target");if(hr)return hr;
    Require(ValidTexture(*d),"deferred targets satisfy real native texture rules");*out=f->Add(KICH_R4_TEXTURE_2D);targets[*out]=*d;return 0;}
int32_t DPipeline(KichR4Handle,const KichR4PipelineDesc* d,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call("deferred pipeline");if(hr)return hr;
    Require(ValidPipeline(*d),"native input layout fields validated");
    const KichR4Handle vs=d->vertex_shader,ps=d->pixel_shader;
    Require(f->Find(vs).kind==KICH_R4_VERTEX_SHADER && f->Find(ps).kind==KICH_R4_PIXEL_SHADER,"pipeline owns valid VS/PS");
    Require(d->vertex_stride==20 || d->vertex_stride==36 || d->vertex_stride==56,"screen/static/model formats explicit");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);native_pipelines[*out]=*d;return 0;}
int32_t DUpdate(KichR4Handle,KichR4Handle handle,const void* data,uint32_t bytes){const int32_t hr=f->Call("deferred update");if(hr)return hr;
    Object& o=f->Find(handle);Require(data && bytes==o.bytes.size(),"whole buffer updated without truncation");memcpy(o.bytes.data(),data,bytes);++f->updates;return 0;}
int32_t DRelease(KichR4Handle,KichR4Handle handle){const int32_t hr=f->Call("deferred release");if(hr)return hr;
    Require(f->objects.erase(handle)==1,"owned resource released once");targets.erase(handle);native_pipelines.erase(handle);shader_entries.erase(handle);++f->released;return 0;}
int32_t DPass(KichR4Handle,const KichR4PassDesc* p){const int32_t hr=f->Call("deferred pass");if(hr)return hr;
    Require(ValidPass(*p) && f->frame.CanDraw(),"explicit valid render pass in active frame");
    for(unsigned i=0;i<p->color_count;++i)if(p->colors[i]){const KichR4Handle h=p->colors[i];const KichR4TextureDesc& t=targets.at(h);
        Require(t.width==p->width && t.height==p->height && (t.bindings&KICH_R4_COLOR_TARGET),"matching MRT dimensions and roles");}
    if(p->depth){const KichR4Handle h=p->depth;const KichR4TextureDesc& t=targets.at(h);
        Require(t.width==p->width && t.height==p->height && (t.bindings&KICH_R4_DEPTH_TARGET),"matching depth target dimensions and role");}
    active_pass=*p;pass_active=true;passes.push_back(*p);return 0;}
int32_t DDraw(KichR4Handle,const KichR4DeferredDrawDesc* d){const int32_t hr=f->Call("deferred draw");if(hr)return hr;
    Require(ValidDeferredDraw(*d) && pass_active && f->frame.CanDraw(),"deferred draw inside valid explicit pass");
    const KichR4DrawIndexedDesc& state=d->state.draw;const KichR4Handle pipeline=state.pipeline;
    const KichR4PipelineDesc& p=native_pipelines.at(pipeline);
    Object& vb=f->Find(state.vertex_buffer);Object& ib=f->Find(state.index_buffer);Object& cb=f->Find(state.constant_buffer);
    Require(vb.kind==KICH_R4_VERTEX_BUFFER && ib.kind==KICH_R4_INDEX_BUFFER && vb.stride==p.vertex_stride &&
        ValidDraw(state,static_cast<uint32_t>(ib.bytes.size()),ib.stride),"native draw validates buffer kinds, strides and ranges");
    Require(!state.depth_mode || active_pass.depth,"depth test always has an explicit depth attachment");
    Require(cb.kind==KICH_R4_CONSTANT_BUFFER && cb.bytes.size()==816,"exact HLSL cbuffer layout");
    for(unsigned i=0;i<8;++i)if(d->textures[i]){const KichR4Handle texture=d->textures[i];
        Require(f->Find(texture).kind==KICH_R4_TEXTURE_2D && texture!=active_pass.depth,"no depth read/write hazard");
        for(unsigned j=0;j<active_pass.color_count;++j)Require(texture!=active_pass.colors[j],"no color read/write hazard");}
    for(unsigned i=0;i<state.index_count;++i){uint32_t at;memcpy(&at,ib.bytes.data()+(state.start_index+i)*4,4);Require(at<vb.bytes.size()/vb.stride,"all indices inside uploaded vertex stream");}
    const KichR4Handle ps=p.pixel_shader;draw_entries.push_back(shader_entries.at(ps));
    kich_r4_lighting::Constants constants;memcpy(&constants,cb.bytes.data(),816);draw_constants.push_back(constants);++f->draws;return 0;}
int32_t DBegin(KichR4Handle h){pass_active=false;return Begin(h);}
int32_t DPresent(KichR4Handle h,uint32_t interval){pass_active=false;return Present(h,interval);}
kich_r4_game::APIs DeferredAPIs(){KichR4LevelSession level;KichR4FrameSession frame={};Configure(level,frame);
    kich_r4_game::APIs a={frame.api,level.bridge.resources,level.bridge.api,level.api,{}, {}};
    a.frame.begin_frame=DBegin;a.frame.present=DPresent;
    a.resources.create_buffer=DBuffer;a.resources.create_texture=DTexture;a.resources.compile_shader=DCompile;a.resources.release_resource=DRelease;
    a.geometry.update_buffer=DUpdate;
    a.deferred={sizeof(a.deferred),KICH_R4_DEFERRED_ABI,sizeof(void*),0,DPipeline,DPass,DDraw};return a;}
KichR4GameFrame DeferredInput(){fixture::Level source;kich_r4_level::Scene scene;
    kich_r4_level::Parse({source.level.data(),static_cast<uint32_t>(source.level.size())},{source.geometry.data(),static_cast<uint32_t>(source.geometry.size())},scene);
    kich_r4_level::Camera camera;camera.Reset(scene.low,scene.high);KichR4GameFrame in={};
    in.window=reinterpret_cast<void*>(1);in.width=960;in.height=540;in.active=in.allowed=true;
    const auto vp=camera.ViewProjection(in.width,in.height);memcpy(in.view_projection,vp.m,64);
    in.position[1]=2;in.position[2]=-3;in.lighting.valid=1;in.lighting.sun_direction[1]=-1;
    for(unsigned i=0;i<3;++i){in.lighting.sun_color[i]=.7f;in.lighting.ambient[i]=.05f;in.lighting.hemi[i]=.2f;in.lighting.fog[i]=.3f;}
    return in;}
}
#endif
