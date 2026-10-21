#ifndef KICH_R4_LEVEL_TEST_RECORDING_H
#define KICH_R4_LEVEL_TEST_RECORDING_H
#include "fixture.h"
#include "../../xr_3da/KichR4LevelSession.h"
#include "../../xr_3da/xrRender_R4/R4FrameController.h"
#include "../../xr_3da/xrRender_R4/R4GeometryRules.h"
#include "../../xr_3da/xrRender_R4/R4ResourceRules.h"
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
using namespace kich_r4;
namespace {
int checks=0;
const int32_t error=static_cast<int32_t>(UINT32_C(0x887a0005));
void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
struct Driver:FrameDriver {
    bool chain,view;Driver():chain(false),view(false){}
    int32_t Create(void*,uint32_t,uint32_t){chain=true;return 0;}void DropView(){view=false;}
    int32_t Resize(uint32_t,uint32_t){Require(!view,"old target dropped before resize");return 0;}
    int32_t MakeView(){view=true;return 0;}void Bind(uint32_t,uint32_t){}void Clear(const float*){}
    int32_t Present(uint32_t,bool){return 0;}int32_t RemovedReason(){return 0;}
    void Close(){chain=view=false;}
};
struct Object {uint32_t kind,stride;fixture::Bytes bytes;};
struct Fixture {
    Driver driver;FrameController frame;std::map<KichR4Handle,Object> objects;std::map<std::string,fixture::Bytes> files;
    uint64_t next;bool live;int calls,fail,leases,opens,closes,progress,cancel_at;int32_t failure;
    uint32_t created,released,draws,updates,texture_calls;std::string failed_name;std::vector<KichR4Handle> bound_textures;
    Fixture():frame(driver),next(10),live(false),calls(0),fail(0),leases(0),opens(0),closes(0),progress(0),cancel_at(0),failure(error),created(0),released(0),draws(0),updates(0),texture_calls(0){fixture::Level data;
        files["$game_levels$/l01_escape\\level"]=data.level;files["$game_levels$/l01_escape\\level.geom"]=data.geometry;
        files["$game_textures$/base.dds"]=fixture::Dds();files["$game_textures$/leaf.dds"]=fixture::Dds();}
    int32_t Call(const char* name){if(++calls==fail){failed_name=name;return failure;}return 0;}
    KichR4Handle Add(uint32_t kind,const void* data=NULL,uint32_t bytes=0,uint32_t stride=0){Object o={kind,stride,{}};
        if(data){const unsigned char* p=static_cast<const unsigned char*>(data);o.bytes.assign(p,p+bytes);}const KichR4Handle h=next++;objects[h]=o;++created;return h;}
    Object& Find(KichR4Handle h){return objects.at(h);}
};
Fixture* f=NULL;
template<class... Args>int32_t Unused(Args...){throw std::runtime_error("unexpected API operation");}
int32_t Open(const char* alias,const char* relative,KichR4FileView* out){memset(out,0,sizeof(*out));const std::string key=std::string(alias)+"/"+relative;
    const auto it=f->files.find(key);if(it==f->files.end())return KICH_R4_E_HANDLE;
    ++f->leases;++f->opens;out->data=it->second.data();out->bytes=static_cast<uint32_t>(it->second.size());out->owner=&it->second;return 0;}
void Close(KichR4FileView* view){if(view->owner){++f->closes;--f->leases;}memset(view,0,sizeof(*view));}
bool Continue(void*){return ++f->progress!=f->cancel_at;}
int32_t CreateDevice(KichR4Handle* out){*out=0;const int32_t hr=f->Call("device");if(hr)return hr;*out=1;f->live=true;return 0;}
int32_t Target(KichR4Handle,const KichR4FrameTargetDesc* d){const int32_t hr=f->Call("target");return hr?hr:f->frame.Create(*d);}
int32_t Begin(KichR4Handle){const int32_t hr=f->Call("begin");return hr?hr:f->frame.Begin();}
int32_t Clear(KichR4Handle,const KichR4FrameColor* c){const int32_t hr=f->Call("clear");return hr?hr:f->frame.Clear(*c);}
int32_t Present(KichR4Handle,uint32_t interval){const int32_t hr=f->Call("present");return hr?hr:f->frame.Present(interval);}
int32_t Resize(KichR4Handle,uint32_t w,uint32_t h){const int32_t hr=f->Call("resize");return hr?hr:f->frame.Resize(w,h);}
int32_t Compile(KichR4Handle,const KichR4ShaderDesc* d,const char* text,uint32_t bytes,KichR4Handle* out,char*,uint32_t){*out=0;
    const int32_t hr=f->Call("compile");if(hr)return hr;Require(d->struct_size==sizeof(*d) && !strcmp(d->entry,"main") && strlen(text)==bytes,"complete SM5 shader source");
    *out=f->Add(d->kind);return 0;}
int32_t Buffer(KichR4Handle,const KichR4BufferDesc* d,const void* data,uint32_t bytes,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call("buffer");if(hr)return hr;Require(ValidBuffer(*d,data,bytes),"viewer buffers satisfy real B2 rules");
    *out=f->Add(d->kind,data,bytes,d->stride);return 0;}
int32_t Pipeline(KichR4Handle,const KichR4PipelineDesc* p,KichR4Handle* out){*out=0;const int32_t hr=f->Call("pipeline");if(hr)return hr;
    Require(ValidPipeline(*p) && p->vertex_stride==sizeof(kich_r4_level::Vertex) && p->elements[1].semantic==KICH_R4_TEXCOORD,"position/UV textured layout");*out=f->Add(KICH_R4_GRAPHICS_PIPELINE);return 0;}
int32_t Texture(KichR4Handle,const KichR4DdsDesc* d,const void* data,uint32_t bytes,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call(++f->texture_calls<=2?"placeholder":"source DDS");if(hr)return hr;kich_r4_dds::Image image={};
    Require(d->struct_size==sizeof(*d) && kich_r4_dds::Parse(data,bytes,d->max_dimension,image),"real DDS reader validates upload");*out=f->Add(KICH_R4_TEXTURE_2D);return 0;}
int32_t Update(KichR4Handle,KichR4Handle h,const void* data,uint32_t bytes){const int32_t hr=f->Call("update");if(hr)return hr;
    Object& cb=f->Find(h);Require(cb.kind==KICH_R4_CONSTANT_BUFFER && cb.bytes.size()==bytes && bytes==80,"MVP / cutoff buffer layout");
    memcpy(cb.bytes.data(),data,bytes);++f->updates;return 0;}
int32_t Draw(KichR4Handle,const KichR4TexturedDrawDesc* d){const int32_t hr=f->Call("draw");if(hr)return hr;
    Require(f->frame.CanDraw() && d->struct_size==sizeof(*d),"textured draw between clear and present");
    Object& vb=f->Find(d->draw.vertex_buffer);Object& ib=f->Find(d->draw.index_buffer);
    Require(vb.kind==KICH_R4_VERTEX_BUFFER && vb.stride==sizeof(kich_r4_level::Vertex) && ib.kind==KICH_R4_INDEX_BUFFER && ValidDraw(d->draw,static_cast<uint32_t>(ib.bytes.size()),4),"real converted buffer ranges");
    for(uint32_t i=0;i<d->draw.index_count;++i){uint32_t vertex;memcpy(&vertex,ib.bytes.data()+(d->draw.start_index+i)*4,4);Require(vertex<vb.bytes.size()/sizeof(kich_r4_level::Vertex),"every submitted index addresses its selected VB");}
    const KichR4Handle texture=d->texture;
    Require(f->Find(texture).kind==KICH_R4_TEXTURE_2D,"live owned texture bound");f->bound_textures.push_back(texture);++f->draws;return 0;}
int32_t Destroy(KichR4Handle,KichR4GeometryReport* out,uint32_t bytes){const int32_t hr=f->Call("destroy");if(hr)return hr;
    f->frame.Close();f->released+=static_cast<uint32_t>(f->objects.size());f->objects.clear();f->live=false;
    if(out){Require(bytes==sizeof(*out),"final report capacity");memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);out->frame=f->frame.Stats();out->resources.struct_size=sizeof(out->resources);
        out->resources.created=f->created;out->resources.released=f->released;out->geometry.struct_size=sizeof(out->geometry);out->geometry.draw_calls=f->draws;out->geometry.buffer_updates=f->updates;}
    return 0;}
void Configure(KichR4LevelSession& level,KichR4FrameSession& frame){
    frame.api={sizeof(frame.api),KICH_R4_FRAME_ABI,sizeof(void*),0,CreateDevice,Unused,Target,Unused,Resize,Begin,Clear,Present,Unused};
    level.bridge.resources={sizeof(level.bridge.resources),KICH_R4_RESOURCE_ABI,sizeof(void*),0,Unused,Unused,Unused,Buffer,Compile,Unused,Unused,Unused,Unused,Unused};
    level.bridge.api={sizeof(level.bridge.api),KICH_R4_GEOMETRY_ABI,sizeof(void*),0,Unused,Update,Unused,Unused,Destroy};
    level.api={sizeof(level.api),KICH_R4_LEVEL_ABI,sizeof(void*),0,Texture,Pipeline,Draw};level.files={Open,Close};level.keep_going=Continue;}
int32_t Render(KichR4LevelSession& level,KichR4FrameSession& frame){const KichR4FrameColor color={sizeof(color),{0,0,0,1}};
    int32_t hr=frame.BeginClear(color);if(hr==0)hr=level.Draw(frame,1280,720);return hr==0?frame.Present():hr;}
int Scenario(int failure,int32_t result=error,int missing=0,int cancel=0){Fixture fixture;f=&fixture;f->fail=failure;f->failure=result;f->cancel_at=cancel;
    if(missing==1)f->files.erase("$game_textures$/base.dds");
    if(missing==2)f->files.erase("$game_levels$/l01_escape\\level.geom");
    if(missing==3)f->files["$game_levels$/l01_escape\\level"].resize(20);
    KichR4LevelSession level;KichR4FrameSession frame={};Configure(level,frame);
    if(frame.Start(reinterpret_cast<void*>(1),960,540)==0 && level.Start(frame,"l01_escape")==0 && Render(level,frame)==0){
        level.Key('T');if(frame.Resize(1280,720)==0)Render(level,frame);}
    KichR4GeometryReport report={};if(level.bridge.Stop(frame,&report)<0)Require(level.bridge.Stop(frame,NULL)==0,"cleanup retry");
    Require(!frame.device && !f->live && f->objects.empty() && !f->driver.chain && !f->driver.view,"partial geometry/textures/device always released");
    Require(!f->leases && f->opens==f->closes,"all borrowed engine readers closed, including on parse failure");
    Require(f->created==f->released,"all resource kinds balanced");
    if(failure && f->failed_name!="source DDS")Require(frame.failure==(result<0?result:KICH_R4_E_STATE),"first native failure retained");
    else if(missing>=2)Require(frame.failure<0 && (!level.detail.empty())==(missing==3),"missing/corrupt level produces diagnostic");
    else if(cancel)Require(level.cancelled && !frame.failure && !frame.rendered,"cancel load without false frame success");
    else{Require(!frame.failure && frame.rendered==2 && level.frames==2 && f->draws==8,"both textured and untextured level frames");
        Require(level.submitted_draws==8 && level.submitted_triangles==8,"64-bit level submission diagnostics");
        Require(f->updates==4,"constants reused for adjacent meshes with same world/cutoff");
        Require(level.texture_fallbacks==((missing || failure)?1u:0u),"optional missing/failed source texture uses checker");
        Require(level.textures_loaded==((missing || failure)?1u:2u),"unique source DDS loaded once");
        for(size_t i=4;i<8;++i)Require(f->bound_textures[i]==level.white,"T switches all meshes to white texture");}
    return f->calls;
}
}
#endif
