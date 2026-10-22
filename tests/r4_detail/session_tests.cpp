#include "../r4_deferred/recording.h"
#include "../../xr_3da/xrRender_R4/R4DetailRules.h"
#include <fstream>
#include <sstream>
namespace {
std::string snapshot_directory;
void BeginSnapshot(uint32_t frame){RenderSnapshotInfo info={};info.frame=frame;strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");
    strncpy(info.directory,snapshot_directory.c_str(),sizeof(info.directory)-1);Require(RenderSnapshot_Request(),"snapshot request");RenderSnapshot_Begin(info);}
std::string EndSnapshot(){RenderSnapshot_End("COMPLETE");std::ifstream in(RenderSnapshot_LastPath());std::ostringstream out;out<<in.rdbuf();return out.str();}
bool Has(const std::string& text,const char* value){return text.find(value)!=std::string::npos;}
void CheckVisibilityAccounting(const std::string& text){size_t at=0;unsigned groups=0;
    while((at=text.find("[VISIBILITY_SUMMARY]",at))!=std::string::npos){const size_t end=text.find("\n[",at+1);const std::string section=text.substr(at,end==std::string::npos?end:end-at);std::istringstream lines(section);std::string line;std::map<std::string,std::string> values;
        while(std::getline(lines,line)){const size_t equals=line.find('=');if(equals!=std::string::npos)values[line.substr(0,equals)]=line.substr(equals+1);}
        Require(values["candidates"]!="NOT_INSTRUMENTED" && values["culled"]!="NOT_INSTRUMENTED" && values["selected"]!="NOT_INSTRUMENTED","fully evaluated frustum paths expose truthful zero sides");
        Require(std::stoull(values["candidates"])==std::stoull(values["culled"])+std::stoull(values["selected"]),"frustum candidates equal culled plus selected");++groups;++at;}
    Require(groups>0,"visibility aggregates emitted");
}
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
    Require(c.world.m[8]>=0 && c.world.m[8]<=2,"still/wave1/wave2 constants");
    if(g_render_snapshot_active){RenderSnapshotProvenance provenance={};RenderSnapshot_GetProvenance(&provenance);
        Require(std::string(provenance.semantic_type)=="DETAIL" && std::string(provenance.queue)=="R4_DETAIL_INSTANCED","detail identity independent of queue");
        Require(provenance.pass_mask_known && (provenance.pass_mask==RENDER_SNAPSHOT_PASS_SUN_SHADOW || provenance.pass_mask==RENDER_SNAPSHOT_PASS_LOCAL_SHADOW || provenance.pass_mask==RENDER_SNAPSHOT_PASS_GBUFFER),"actual detail pass mask");
        Require(provenance.uv_count==3 && std::string(provenance.uv_source)=="DETAIL_INDEXED_MESH_QUANTIZED_BASE_UV","bounded source detail UV samples");
        RenderSnapshotDraw event={};event.struct_size=sizeof(event);event.submitted=1;event.topology=4;event.index_count=draw.index_count;event.instance_count=desc->instance_count;
        event.first_instance=desc->first_instance;event.vertex_capacity=static_cast<uint32_t>(vb.bytes.size()/28);strcpy(event.vertex_shader,"VSDetail");strncpy(event.pixel_shader,ps.c_str(),sizeof(event.pixel_shader)-1);RenderSnapshot_Draw(&event);}
    ++detail_draws;return 0;}
kich_r4_detail::Frame Source(){kich_r4_detail::Frame source;source.generation=42;source.ClearFrame(77);
    kich_r4_detail::Mesh mesh;mesh.texture="base";mesh.shader="detail";mesh.name="test_details";
    mesh.vertices.resize(3);mesh.vertices[0]={};mesh.vertices[1]={};mesh.vertices[2]={};mesh.vertices[1].position[0]=1;mesh.vertices[2].position[1]=1;
    mesh.indices={0,1,2};source.meshes.push_back(mesh);mesh.name="loaded_but_inactive";source.meshes.push_back(mesh);KichR4DetailInstance instance={};instance.row[0][0]=instance.row[1][1]=instance.row[2][2]=1;
    for(unsigned i=0;i<4;++i)source.Append(instance);
    source.groups={{0,0,0,2},{0,1,2,1},{0,2,3,1}};source.variants[0]=2;source.variants[1]=source.variants[2]=1;return source;}
int ScenarioDetail(int fail=0){Fixture fixture;f=&fixture;f->fail=fail;ResetDeferredRecorder();detail_pipelines.clear();uploaded.clear();instance_uploads=detail_draws=0;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(DeferredAPIs(),files,"l01_escape",NULL);
    auto source=Source();auto& details=game.deferred.details;details.source=&source;details.api={sizeof(details.api),KICH_R4_DETAIL_ABI,sizeof(void*),0,DetailPipeline,Instances,UploadInstances,DetailDraw};game.deferred.native_mode=true;
    auto input=DeferredInput();input.engine_frame=77;if(!fail)BeginSnapshot(input.engine_frame);
    const int32_t result=game.Render(input);const int positions=f->calls;
    if(!fail){Require(result==1 && details.draw_calls==3 && details.shadow_draw_calls==3 && detail_draws==6,"details participate in G-buffer and sun shadow");
        Require(instance_uploads==1 && details.vertex_bytes==168 && details.index_bytes==12,"upload instances once, retain one GPU copy per loaded mesh");
        Require(details.uploaded_instances==4 && details.capacity==256 && details.submitted_instances==4 && details.shadow_submitted_instances==4,"allocated capacity and actual upload/submission counts separated");
        details.SnapshotSummary(input.engine_frame,true);const std::string snapshot=EndSnapshot();
        Require(Has(snapshot,"detail_variant=0") && Has(snapshot,"detail_variant=1") && Has(snapshot,"detail_variant=2"),"Still/Wave1/Wave2 retained independently of source shader");
        Require(Has(snapshot,"[DETAIL_PASS sun_0]") && Has(snapshot,"[DETAIL_PASS camera_geometry]") && Has(snapshot,"still_submitted_instances=2"),"per-phase instance summaries");
        Require(Has(snapshot,"loaded_meshes=2") && Has(snapshot,"active_meshes=1") && Has(snapshot,"instance_allocated_bytes=16384") && Has(snapshot,"instance_upload_bytes=256"),"loaded/active and allocated/uploaded memory summaries");
        Require(Has(snapshot,"status=CURRENT_UPLOADED") && Has(snapshot,"NO_PER_SHADOW_INSTANCE_CULL") && Has(snapshot,"CAMERA_FRUSTUM"),"selection scope and static visibility instrumented");
        CheckVisibilityAccounting(snapshot);
        const auto vb=details.meshes[0].vb,stream=details.instance_buffer;const auto created=f->created;
        ++input.engine_frame;source.engine_frame=input.engine_frame;Require(game.Render(input)==1,"next frame renders");
        Require(details.meshes[0].vb==vb && details.instance_buffer==stream && created==f->created && instance_uploads==2,"warm frame reuses meshes and instance stream");
        source.ClearFrame(++input.engine_frame);Require(game.Render(input)==1 && details.draw_calls==0 && details.shadow_draw_calls==0,"empty frame never replays stale details");
        BeginSnapshot(input.engine_frame);details.SnapshotSummary(input.engine_frame,true);const std::string empty=EndSnapshot();
        Require(Has(empty,"status=CURRENT_EMPTY") && Has(empty,"instance_upload_bytes=0") && Has(empty,"instance_allocated_bytes=16384"),"empty frame reports retained allocation without upload");
        ++input.engine_frame;Require(game.Render(input)==1 && details.uploaded_instances==0 && details.submitted_instances==0,"stale source never uploads or submits");
        BeginSnapshot(input.engine_frame);details.SnapshotSummary(input.engine_frame,true);Require(Has(EndSnapshot(),"status=UNPREPARED_FOR_FRAME"),"stale frontend explicitly identified");
        BeginSnapshot(input.engine_frame);details.SnapshotSummary(input.engine_frame,false);const std::string menu=EndSnapshot();Require(Has(menu,"status=SCENE_NOT_SUBMITTED") && Has(menu,"visible_instances=0"),"menu does not report prior scene as current");
        source=Source();source.engine_frame=++input.engine_frame;source.generation=43;Require(game.Render(input)==1 && details.meshes[0].vb!=vb && !f->objects.count(vb),"scene generation invalidates old mesh handles");
        input.lighting.count=2;input.lighting.shadow_mask=3;
        for(unsigned i=0;i<2;++i){auto& light=input.lighting.lights[i];light.type=i?1:2;light.range=20;light.direction[1]=-1;light.cone=1;light.color[0]=light.color[1]=light.color[2]=1;}
        source.engine_frame=++input.engine_frame;BeginSnapshot(input.engine_frame);Require(game.Render(input)==1,"spot and point shadow telemetry frame renders");details.SnapshotSummary(input.engine_frame,true);const std::string shadows=EndSnapshot();
        Require(details.shadow_draw_calls==24 && details.shadow_submitted_instances==32 && details.draw_calls==3,"camera-selected details reused by sun, one spot and six point faces");
        Require(Has(shadows,"[DETAIL_PASS local_0_face_0]") && Has(shadows,"[DETAIL_PASS local_1_face_5]") && Has(shadows,"light_scope=FRAME_LOCAL_LIGHT_ARRAY_INDEX"),"shadow light index and each point face identified");
        Require(Has(shadows,"[LIGHT local_0]") && Has(shadows,"type=SPOT") && Has(shadows,"[LIGHT local_1]") && Has(shadows,"type=POINT"),"light descriptors retain actual type");
        CheckVisibilityAccounting(shadows);
        source.groups[0].first=UINT32_MAX;Require(game.Render(input)<0,"overflowing detail group rejected");
    }else if(f->failed_name!="source DDS")Require(result<0,"native detail failures propagate without fake successful frame");
    KichR4GeometryReport report={};if(game.Stop(report)<0)game.Stop(report);
    Require(!f->live && f->objects.empty() && f->created==f->released,"partial details and streams released by one owner");return positions;
}
}
int main(int argc,char** argv){try{(void)&Scenario;(void)&DDraw;Require(argc==2,"snapshot output directory");snapshot_directory=argv[1];
    Require(!ValidDetailCapacity(0) && !ValidDetailCapacity(UINT32_MAX) && ValidDetailCapacity(KICH_R4_MAX_DETAIL_INSTANCES),"instance budget boundaries");
    Require(!ValidDetailRange(UINT32_MAX,1,16) && !ValidDetailRange(1,UINT32_MAX,16) && !ValidDetailRange(0,0,16) && ValidDetailRange(16,1,17),"overflow-safe instance ranges");
    const int positions=ScenarioDetail();for(int i=1;i<=positions;++i)ScenarioDetail(i);
    printf("G3a native detail submission: %d checks; %d failure positions passed\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
