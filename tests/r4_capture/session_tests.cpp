#include "../r4_level/recording.h"
#include "fixture.h"
#include "../../xr_3da/KichR4GameSession.h"
#include "../../xr_3da/xrRender_R4/R4StreamRules.h"
namespace {
std::vector<KichR4StreamDrawDesc> submitted;
int32_t UploadBuffer(KichR4Handle,const KichR4BufferDesc* d,const void* data,uint32_t bytes,KichR4Handle* out){*out=0;
    const int32_t hr=f->Call("upload buffer");if(hr)return hr;Require(ValidBuffer(*d,data,bytes),"full-sized native buffer description");
    *out=f->Add(d->kind,data,bytes,d->stride);if(!data)f->Find(*out).bytes.resize(d->byte_size);return 0;}
int32_t Upload(KichR4Handle,KichR4Handle handle,const void* data,uint32_t bytes){const int32_t hr=f->Call("upload");if(hr)return hr;
    Object& o=f->Find(handle);Require(data && bytes==o.bytes.size(),"uploads cover complete existing native buffer");memcpy(o.bytes.data(),data,bytes);++f->updates;return 0;}
int32_t Release(KichR4Handle,KichR4Handle handle){const int32_t hr=f->Call("release replaced");if(hr)return hr;Require(f->objects.erase(handle)==1,"replaced upload buffer released exactly once");++f->released;return 0;}
int32_t StreamPipeline(KichR4Handle,const KichR4PipelineDesc* d,KichR4Handle* out){*out=0;const int32_t hr=f->Call("stream pipeline");if(hr)return hr;
    Require(ValidPipeline(*d) && d->vertex_stride==28 && d->element_count==3 && d->elements[0].format==KICH_R4_FLOAT4 && d->elements[2].format==KICH_R4_UNORM8X4,"posed clip-space / UV / RGBA pipeline");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);return 0;}
int32_t StreamDraw(KichR4Handle,const KichR4StreamDrawDesc* d){const int32_t hr=f->Call("stream draw");if(hr)return hr;
    Require(ValidStreamDraw(*d) && f->frame.CanDraw(),"stream draws inside owned native frame");
    const Object& ib=f->Find(d->draw.index_buffer);const Object& vb=f->Find(d->draw.vertex_buffer);
    Require(ValidDraw(d->draw,static_cast<uint32_t>(ib.bytes.size()),ib.stride),"native index range validated");
    for(uint32_t i=0;i<d->draw.index_count;++i){uint32_t at;memcpy(&at,ib.bytes.data()+(d->draw.start_index+i)*4,4);Require(uint64_t(at)*28<vb.bytes.size(),"every submitted index addresses uploaded posed vertices");}
    Require(f->Find(d->texture).kind==KICH_R4_TEXTURE_2D,"stream uses native DDS resources");submitted.push_back(*d);return 0;}
kich_r4_game::APIs APIs(){KichR4LevelSession level;KichR4FrameSession frame={};Configure(level,frame);
    kich_r4_game::APIs a={frame.api,level.bridge.resources,level.bridge.api,level.api,{}};
    a.resources.create_buffer=UploadBuffer;a.resources.release_resource=Release;a.geometry.update_buffer=Upload;
    a.stream={sizeof(a.stream),KICH_R4_STREAM_ABI,sizeof(void*),0,StreamPipeline,StreamDraw};return a;}
KichR4GameFrame Input(){fixture::Level source;kich_r4_level::Scene scene;kich_r4_level::Parse({source.level.data(),static_cast<uint32_t>(source.level.size())},
    {source.geometry.data(),static_cast<uint32_t>(source.geometry.size())},scene);kich_r4_level::Camera camera;camera.Reset(scene.low,scene.high);
    KichR4GameFrame input={};input.width=960;input.height=540;input.window=reinterpret_cast<void*>(1);input.active=input.allowed=true;
    const auto vp=camera.ViewProjection(960,540);memcpy(input.view_projection,vp.m,sizeof(vp.m));return input;}
kich_r4_capture::Batch MakeBatch(){using namespace kich_r4_capture;Batch b;b.Begin(960,540,0);b.renderer_seen=b.ui_seen=true;
    const fixture::Bytes raw=capture_fixture::Model(2);const std::shared_ptr<Mesh> mesh=Decode({raw.data(),static_cast<uint32_t>(raw.size())});
    Matrix m=kich_r4_scene::Identity(),bones[2]={m,m};Style s;s.max_z=.02f;b.Model(reinterpret_cast<void*>(1),*mesh,m,m,bones,2,3,3,1,s);
    s.max_z=1;b.Model(reinterpret_cast<void*>(2),*mesh,m,m,bones,2,3,3,1,s);
    const uint32_t at=static_cast<uint32_t>(b.vertices.size());b.vertices.push_back(b.Screen(0,0,0,1,0,0,0xffffffff));
    b.vertices.push_back(b.Screen(40,0,0,1,1,0,0xffffffff));b.vertices.push_back(b.Screen(0,40,0,1,0,1,0xffffffff));
    Style ui;ui.blend=1;ui.clamp=1;ui.depth=0;ui.write=0;ui.cull=2;ui.scissor=true;ui.rect[2]=300;ui.rect[3]=200;
    Command cmd={static_cast<uint32_t>(b.indices.size()),3,2,"base",ui};b.indices.push_back(at);b.indices.push_back(at+1);b.indices.push_back(at+2);b.commands.push_back(cmd);b.ui=1;return b;}
int ComposedScenario(int failure=0,int32_t result=error,bool grow=false){Fixture fixture;f=&fixture;f->fail=failure;f->failure=result;submitted.clear();
    const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(APIs(),fs,"l01_escape",NULL);kich_r4_capture::Batch batch=MakeBatch();
    KichR4GameFrame input=Input();const int32_t rendered=game.Render(input,&batch);
    const int calls=f->calls;
    if(!failure){Require(rendered==1 && submitted.size()==3,"one composed world/models/hands/HUD frame presented");
        Require(submitted[0].max_depth==1 && submitted[0].draw.depth_mode==1 && submitted[1].max_depth==.02f && !submitted[2].draw.depth_mode && submitted[2].blend_enable,"world -> hands -> HUD ordering and depth/blend contracts");
        const KichR4Handle vb=game.stream.vb;const uint32_t created=f->created;
        Require(game.Render(input,&batch)==1 && game.stream.vb==vb && f->created==created,"native upload buffers and pipeline reused across frames");
        if(grow){batch.vertices.resize(300);Require(game.Render(input,&batch)==1 && game.stream.vb!=vb && !f->objects.count(vb),"upload capacity grows and releases old buffer");}}
    else if(f->failed_name!="source DDS")Require(rendered<0,"partial stream failure reaches legacy presenter decision");
    KichR4GeometryReport report={};if(game.Stop(report)<0 && game.frame.device)Require(game.Stop(report)==0,"cleanup retry");
    Require(!game.frame.device && !f->live && f->objects.empty() && f->created==f->released && !f->leases,"all static/stream/texture/shader owners released after every failure");return calls;}
}
int main(){try{(void)&Scenario;const int positions=ComposedScenario();ComposedScenario(0,error,true);
    for(int i=1;i<=positions;++i){ComposedScenario(i);ComposedScenario(i,7);}
    printf("B4.3b composed game session / reuse / cleanup: %d checks; %d failure positions and unexpected positive statuses\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
