#include "../r4_deferred/recording.h"
#include "../../xr_3da/xrRender_R4/R4DetailRules.h"
namespace {
std::map<KichR4Handle,KichR4DetailPipelineDesc> detail_pipelines;
std::map<KichR4Handle,uint32_t> uploaded;
unsigned instance_uploads=0,detail_draws=0;
int32_t DetailPipeline(KichR4Handle,const KichR4DetailPipelineDesc* d,KichR4Handle* out){*out=0;const int32_t hr=f->Call("detail pipeline");if(hr)return hr;
    Require(d->struct_size==sizeof(*d) && shader_entries.at(d->vs)=="VSDetail","native detail vertex shader");
    Require(shader_entries.at(d->ps)=="PSGeometry" || shader_entries.at(d->ps)=="PSShadow","native detail geometry/shadow shader");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);detail_pipelines[*out]=*d;return 0;}
int32_t Instances(KichR4Handle,uint32_t count,KichR4Handle* out){*out=0;const int32_t hr=f->Call("instance allocation");if(hr)return hr;
    Require(ValidDetailCapacity(count),"bounded dynamic stream");*out=f->Add(KICH_R4_VERTEX_BUFFER,NULL,0,64);f->Find(*out).bytes.resize(count*64);return 0;}
int32_t UploadInstances(KichR4Handle,KichR4Handle handle,const KichR4DetailInstance* data,uint32_t count){const int32_t hr=f->Call("instance upload");if(hr)return hr;
    Object& o=f->Find(handle);Require(data && ValidDetailCapacity(count) && count<=o.bytes.size()/64,"only initialized instance prefix");
    memcpy(o.bytes.data(),data,count*64);uploaded[handle]=count;++instance_uploads;return 0;}
int32_t DetailDraw(KichR4Handle,const KichR4DetailDrawDesc* desc){const int32_t hr=f->Call("instanced draw");if(hr)return hr;
    const auto& draw=desc->surface.state.draw;const auto& state=desc->surface.state;
    Require(desc->struct_size==sizeof(*desc) && ValidDeferredDraw(desc->surface) && pass_active && f->frame.CanDraw(),"detail draw inside native deferred pass");
    Require(ValidDetailRange(desc->first_instance,desc->instance_count,uploaded.at(desc->instance_buffer)),"draw never sees stale capacity tail");
    const Object& vb=f->Find(draw.vertex_buffer);const Object& ib=f->Find(draw.index_buffer);
    Require(vb.stride==28 && ib.stride==2 && ValidDraw(draw,static_cast<uint32_t>(ib.bytes.size()),2),"one mesh copy, 16-bit source indices");
    for(unsigned i=0;i<draw.index_count;++i){uint16_t index;memcpy(&index,ib.bytes.data()+i*2,2);Require(index<vb.bytes.size()/28,"detail index references mesh, not duplicated batch");}
    Require(state.blend_enable==0 && state.depth_write==1 && draw.depth_mode==1 && draw.cull_mode==KICH_R4_CULL_NONE,"cutout depth and two-sided geometry");
    kich_r4_lighting::Constants c;memcpy(&c,f->Find(draw.constant_buffer).bytes.data(),sizeof(c));
    Require(c.alpha[1]==7 && std::fabs(c.alpha[0]-200.f/255)<1.e-6f,"same alpha cutout in geometry and shadow");
    const auto& p=detail_pipelines.at(draw.pipeline);const std::string ps=shader_entries.at(p.ps);
    Require((ps=="PSGeometry" && active_pass.color_count==2) || (ps=="PSShadow" && active_pass.color_count==0),"detail pipeline matches target pass");
    Require(c.world.m[8]>=0 && c.world.m[8]<=2,"still/wave1/wave2 constants");++detail_draws;return 0;}
kich_r4_detail::Frame Source(){kich_r4_detail::Frame source;source.generation=42;source.ClearFrame(77);
    kich_r4_detail::Mesh mesh;mesh.texture="base";mesh.shader="detail";mesh.name="test_details";
    mesh.vertices.resize(3);mesh.vertices[0]={};mesh.vertices[1]={};mesh.vertices[2]={};mesh.vertices[1].position[0]=1;mesh.vertices[2].position[1]=1;
    mesh.indices={0,1,2};source.meshes.push_back(mesh);KichR4DetailInstance instance={};instance.row[0][0]=instance.row[1][1]=instance.row[2][2]=1;
    for(unsigned i=0;i<4;++i)source.Append(instance);
    source.groups={{0,0,0,2},{0,1,2,1},{0,2,3,1}};source.variants[0]=2;source.variants[1]=source.variants[2]=1;return source;}
int ScenarioDetail(int fail=0){Fixture fixture;f=&fixture;f->fail=fail;ResetDeferredRecorder();detail_pipelines.clear();uploaded.clear();instance_uploads=detail_draws=0;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(DeferredAPIs(),files,"l01_escape",NULL);
    auto source=Source();auto& details=game.deferred.details;details.source=&source;details.api={sizeof(details.api),KICH_R4_DETAIL_ABI,sizeof(void*),0,DetailPipeline,Instances,UploadInstances,DetailDraw};game.deferred.native_mode=true;
    auto input=DeferredInput();input.engine_frame=77;const int32_t result=game.Render(input);const int positions=f->calls;
    if(!fail){Require(result==1 && details.draw_calls==3 && details.shadow_draw_calls==3 && detail_draws==6,"details participate in G-buffer and sun shadow");
        Require(instance_uploads==1 && details.vertex_bytes==84 && details.index_bytes==6,"upload instances once, retain one GPU mesh");
        const auto vb=details.meshes[0].vb,stream=details.instance_buffer;const auto created=f->created;
        ++input.engine_frame;source.engine_frame=input.engine_frame;Require(game.Render(input)==1,"next frame renders");
        Require(details.meshes[0].vb==vb && details.instance_buffer==stream && created==f->created && instance_uploads==2,"warm frame reuses meshes and instance stream");
        source.ClearFrame(++input.engine_frame);Require(game.Render(input)==1 && details.draw_calls==0 && details.shadow_draw_calls==0,"empty frame never replays stale details");
        source=Source();source.engine_frame=++input.engine_frame;source.generation=43;Require(game.Render(input)==1 && details.meshes[0].vb!=vb && !f->objects.count(vb),"scene generation invalidates old mesh handles");
        source.groups[0].first=UINT32_MAX;Require(game.Render(input)<0,"overflowing detail group rejected");
    }else if(f->failed_name!="source DDS")Require(result<0,"native detail failures propagate without fake successful frame");
    KichR4GeometryReport report={};if(game.Stop(report)<0)game.Stop(report);
    Require(!f->live && f->objects.empty() && f->created==f->released,"partial details and streams released by one owner");return positions;
}
}
int main(){try{(void)&Scenario;(void)&DDraw;
    Require(!ValidDetailCapacity(0) && !ValidDetailCapacity(UINT32_MAX) && ValidDetailCapacity(KICH_R4_MAX_DETAIL_INSTANCES),"instance budget boundaries");
    Require(!ValidDetailRange(UINT32_MAX,1,16) && !ValidDetailRange(1,UINT32_MAX,16) && !ValidDetailRange(0,0,16) && ValidDetailRange(16,1,17),"overflow-safe instance ranges");
    const int positions=ScenarioDetail();for(int i=1;i<=positions;++i)ScenarioDetail(i);
    printf("G3a native detail submission: %d checks; %d failure positions passed\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
