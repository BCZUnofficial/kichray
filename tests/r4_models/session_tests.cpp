#include "../r4_level/recording.h"
#include "../../xr_3da/KichR4ModelSession.h"
#include "../../xr_3da/xrRender_R4/R4ModelRules.h"
#include "../../xr_3da/xrRender_R4/R4DeferredRules.h"
#include <limits>
#include <new>
#include <fstream>
#include <sstream>
namespace {
std::map<KichR4Handle,KichR4ModelPipelineDesc> model_pipelines;
std::set<KichR4Handle> initialized_poses;
std::vector<KichR4ModelDrawDesc> model_draws;
std::vector<kich_r4_lighting::Constants> constants_drawn;
unsigned uploads=0,allocations=0;
int32_t ModelCompile(KichR4Handle,const KichR4ShaderDesc* d,const char* source,uint32_t,KichR4Handle* out,char*,uint32_t){*out=0;const int32_t hr=f->Call("model shader");if(hr)return hr;
    Require(!strcmp(d->entry,"VSModelGPU") && !strncmp(source,"#define KICH_R4_MODEL_BONE_CAPACITY ",35),"GPU model shader entry and capacity variant");*out=f->Add(KICH_R4_VERTEX_SHADER);return 0;}
int32_t ModelPipeline(KichR4Handle,const KichR4ModelPipelineDesc* d,KichR4Handle* out){*out=0;const int32_t hr=f->Call("model pipeline");if(hr)return hr;
    Require(d->struct_size==sizeof(*d) && f->Find(d->vs).kind==KICH_R4_VERTEX_SHADER && f->Find(d->ps).kind==KICH_R4_PIXEL_SHADER,"model pipeline binds explicit shader kinds");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);model_pipelines[*out]=*d;return 0;}
int32_t Pose(KichR4Handle,uint32_t capacity,KichR4Handle* out){*out=0;const int32_t hr=f->Call("pose allocation");if(hr)return hr;
    *out=f->Add(KICH_R4_CONSTANT_BUFFER);Require(KichR4ModelValidCapacity(capacity),"supported palette capacity class");f->Find(*out).bytes.resize(KichR4ModelPaletteBytes(capacity));++allocations;return 0;}
int32_t Upload(KichR4Handle,KichR4Handle h,const KichR4ModelPose* p){const int32_t hr=f->Call("pose upload");if(hr)return hr;
    Require(p && ValidModelPose(*p) && f->Find(h).bytes.size()>=16+p->bone_count*64,"validated compact palette upload");
    std::fill(f->Find(h).bytes.begin(),f->Find(h).bytes.end(),0);memcpy(f->Find(h).bytes.data(),p,16+p->bone_count*64);initialized_poses.insert(h);++uploads;return 0;}
int32_t ModelUpdate(KichR4Handle,KichR4Handle h,const void* p,uint32_t bytes){const int32_t hr=f->Call("model constants");if(hr)return hr;
    Require(bytes==sizeof(kich_r4_lighting::Constants) && f->Find(h).bytes.size()==bytes,"frame constants use current b0 ABI");memcpy(f->Find(h).bytes.data(),p,bytes);return 0;}
int32_t ModelRelease(KichR4Handle,KichR4Handle h){const int32_t hr=f->Call("model release");if(hr)return hr;
    Require(f->objects.erase(h)==1,"expired geometry releases exactly once");initialized_poses.erase(h);++f->released;return 0;}
int32_t ModelDraw(KichR4Handle,const KichR4ModelDrawDesc* d){const int32_t hr=f->Call("model draw");if(hr)return hr;
    const KichR4DrawIndexedDesc& draw=d->surface.state.draw;
    Require(d->struct_size==sizeof(*d) && ValidDeferredDraw(d->surface),"native state descriptor valid");
    const Object& vb=f->Find(draw.vertex_buffer);const Object& ib=f->Find(draw.index_buffer);
    Require(vb.stride==sizeof(KichR4ModelVertex) && ib.stride==2 && ValidDraw(draw,static_cast<uint32_t>(ib.bytes.size()),2),"immutable compact source vertices and uint16 indices");
    Require(initialized_poses.count(d->pose_buffer)==1,"draw uses initialized palette");
    Require(model_pipelines.count(draw.pipeline)==1,"model pipeline cannot become generic streamed pipeline");
    Require(KichR4ModelPaletteBytes(model_pipelines.at(draw.pipeline).bone_capacity)==f->Find(d->pose_buffer).bytes.size(),"shader declared palette matches actual buffer class exactly");
    for(uint32_t i=0;i<draw.index_count;++i){uint16_t at;memcpy(&at,ib.bytes.data()+(draw.start_index+i)*2,2);Require(at<vb.bytes.size()/sizeof(KichR4ModelVertex),"LOD source index range");}
    kich_r4_lighting::Constants c;memcpy(&c,f->Find(draw.constant_buffer).bytes.data(),sizeof(c));constants_drawn.push_back(c);model_draws.push_back(*d);
    if(g_render_snapshot_active){RenderSnapshotDraw event={};event.struct_size=sizeof(event);event.submitted=1;event.topology=4;event.index_count=draw.index_count;event.instance_count=1;event.textures[0]=d->surface.textures[0];RenderSnapshot_Draw(&event);}return 0;}
struct Harness {
    Fixture fixture;KichR4LevelSession level;KichR4FrameSession frame;KichR4ModelSession models;KichR4Handle cb,white,ps[6];kich_r4_lighting::Constants base;
    Harness(int fail=0):frame(),cb(0),white(0){f=&fixture;f->fail=fail;f->live=true;model_pipelines.clear();initialized_poses.clear();model_draws.clear();constants_drawn.clear();uploads=allocations=0;
        Configure(level,frame);frame.device=1;level.bridge.resources.compile_shader=ModelCompile;level.bridge.resources.release_resource=ModelRelease;level.bridge.api.update_buffer=ModelUpdate;
        models.api={sizeof(models.api),KICH_R4_MODEL_ABI,sizeof(void*),0,ModelPipeline,Pose,Upload,ModelDraw};
        memset(&base,0,sizeof(base));base.world=base.mvp=base.normal_matrix=kich_r4_scene::Identity();base.camera[2]=-2;
        cb=f->Add(KICH_R4_CONSTANT_BUFFER,&base,sizeof(base));white=f->Add(KICH_R4_TEXTURE_2D);for(unsigned i=0;i<6;++i)ps[i]=f->Add(KICH_R4_PIXEL_SHADER);
    }
    int32_t Start(){return models.Start(frame,level,"model shader",ps[0],ps[1],ps[2],ps[3],ps[4],ps[5],white);}
    int32_t Draw(kich_r4_capture::Batch& batch,KichR4ModelSession::Pass pass){return models.Draw(frame,level,cb,base,batch,pass,1,"models_test");}
    ~Harness(){f=&fixture;const int32_t before=f->fail;f->fail=0;KichR4GeometryReport report={};Destroy(1,&report,sizeof(report));f->fail=before;
        Require(f->objects.empty() && f->created==f->released,"one device owner releases partial model resources");}
};
std::shared_ptr<kich_r4_capture::Mesh> Source(bool skinned=true){std::shared_ptr<kich_r4_capture::Mesh> m(new kich_r4_capture::Mesh());
    m->skinned=skinned;m->two_weights=skinned;m->bones=skinned?2:0;m->vertices.resize(3);m->indices={0,1,2,0,2,1};
    for(unsigned i=0;i<3;++i){auto& v=m->vertices[i];v={};v.p[0]=i==1?.5f:0;v.p[1]=i==2?.5f:0;v.p[2]=.5f;v.normal=kich_r4_capture::PackNormal(0,0,1);v.weight=.333f;v.bone[1]=skinned?1:0;}
    return m;
}
KichR4ModelSession::Command Command(const std::shared_ptr<const kich_r4_capture::Mesh>& m){KichR4ModelSession::Command c;c.mesh=m;c.world=c.vp=kich_r4_scene::Identity();c.vertex_count=3;c.first=3;c.count=3;
    c.skin_mode=KICH_R4_SKIN_HARDWARE;c.bones.assign(m->bones,kich_r4_scene::Identity());c.bounds_known=c.bounds.valid=true;
    c.bounds.min[0]=c.bounds.min[1]=0;c.bounds.min[2]=.5f;c.bounds.max[0]=c.bounds.max[1]=.5f;c.bounds.max[2]=.5f;return c;}
int FailureScenario(int failure){Harness h(failure);auto mesh=Source();kich_r4_capture::Batch batch;batch.engine_frame=17;batch.native_models.push_back(Command(mesh));
    int32_t result=h.Start();if(result>=0)result=h.models.Prepare(h.frame,h.level,batch);if(result>=0)result=h.Draw(batch,KichR4ModelSession::Geometry);
    if(failure)Require(result<0 && h.frame.failure<0,"allocation/upload/draw failure propagates without successful submission");else Require(result==0 && model_draws.size()==1,"native rigid/skinned draw setup succeeds");return f->calls;}
void ReuseAndRouting(){Harness h;Require(h.Start()==0,"start native model pipelines");auto mesh=Source();auto rigid=Source(false);kich_r4_capture::Batch batch;batch.engine_frame=42;
    auto a=Command(mesh),b=a,r=Command(rigid),hud=a,forward=a,distort=a;b.first=0;b.bones[0].m[12]=.25f;b.skin_mode=KICH_R4_SKIN_SOFTWARE;
    hud.layer=1;hud.vp.m[0]=3;hud.style.min_z=.1f;hud.style.max_z=.25f;forward.style.blend=1;forward.effect=6;distort.effect=5;distort.style.blend=1;
    batch.native_models={a,b,r,hud,forward,distort};Require(h.models.Prepare(h.frame,h.level,batch)==0,"prepare immutable models and separate poses");
    Require(h.models.meshes.size()==2 && h.models.mesh_uploads==2 && h.models.vertex_bytes==6*sizeof(KichR4ModelVertex) && h.models.index_bytes==24,"clones share exactly one compact source allocation");
    Require(h.models.pose_uploads==5 && batch.vertices.empty() && batch.indices.empty(),"only skeletal pose data uploaded per instance, no expanded CPU geometry");
    const auto vb=h.models.meshes.at(mesh.get()).vb;const unsigned uploaded=uploads;const auto created=f->created;
    for(unsigned i=0;i<KichR4ModelSession::PassCount;++i)Require(h.Draw(batch,static_cast<KichR4ModelSession::Pass>(i))==0,"all explicit passes draw");
    Require(h.models.draw_calls[KichR4ModelSession::Geometry]==3 && h.models.draw_calls[KichR4ModelSession::Shadow]==3,"opaque rigid/skinned clones in geometry and shadow");
    Require(h.models.draw_calls[KichR4ModelSession::Forward]==1 && h.models.draw_calls[KichR4ModelSession::ForwardAdd]==1,"blended model forward passes");
    Require(h.models.draw_calls[KichR4ModelSession::HUD]==1 && h.models.draw_calls[KichR4ModelSession::HUDAdd]==1 && h.models.draw_calls[KichR4ModelSession::Distortion]==1,"hands and distortion routed exclusively");
    Require(uploads==uploaded,"multiple passes reuse frame palettes");
    Require(model_draws[0].surface.state.draw.vertex_buffer==model_draws[1].surface.state.draw.vertex_buffer && model_draws[0].pose_buffer!=model_draws[1].pose_buffer,"clones share geometry but never pose state");
    Require(model_draws[0].surface.state.draw.start_index==3 && constants_drawn[8].mvp.m[0]==3,"LOD offset and weapon FOV preserved");
    Require(model_pipelines.at(model_draws[8].surface.state.draw.pipeline).ps==h.ps[2],"HUD uses sun-lit forward shader");
    Require(model_draws[8].surface.state.min_depth==.1f && model_draws[8].surface.state.max_depth==.25f,"HUD near depth range preserved");
    KichR4ModelPose uploaded_a={},uploaded_b={};memcpy(&uploaded_a,f->Find(model_draws[0].pose_buffer).bytes.data(),f->Find(model_draws[0].pose_buffer).bytes.size());memcpy(&uploaded_b,f->Find(model_draws[1].pose_buffer).bytes.data(),f->Find(model_draws[1].pose_buffer).bytes.size());
    Require(uploaded_a.skin_mode==KICH_R4_SKIN_HARDWARE && uploaded_b.skin_mode==KICH_R4_SKIN_SOFTWARE && uploaded_b.bones[0][12]==.25f,"copied instance palettes and raw/quantized skin modes survive upload");
    kich_r4_capture::SourceVertex packed;memcpy(&packed,f->Find(vb).bytes.data(),sizeof(packed));Require(packed.normal==mesh->vertices[0].normal && packed.weight==.333f,"normal bits and unquantized source weight remain immutable");
    ++batch.engine_frame;Require(h.models.Prepare(h.frame,h.level,batch)==0,"warm next frame");
    Require(h.models.mesh_uploads==0 && h.models.meshes.at(mesh.get()).vb==vb && f->created==created,"no immutable geometry or palette allocation on warm frame");
    h.base.mvp.m[12]=10;const size_t before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && model_draws.size()==before,"camera frustum can reject known world bounds");
    h.base.mvp=kich_r4_scene::Identity();Require(h.Draw(batch,KichR4ModelSession::Shadow)==0 && model_draws.size()==before+3,"off-camera models remain available to independent light frustum");
    batch.Begin(640,480,44);Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0 && !h.models.pose_uploads && !h.models.draw_calls[0],"empty frame never replays stale commands");
    a.mesh.reset();b.mesh.reset();r.mesh.reset();hud.mesh.reset();forward.mesh.reset();distort.mesh.reset();mesh.reset();rigid.reset();
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.models.meshes.empty() && !h.models.vertex_bytes && !h.models.index_bytes && !f->objects.count(vb),"last visual deletion retires shared source GPU resources");
}
void PointerReuse(){Harness h;Require(h.Start()==0,"pointer reuse setup");typedef kich_r4_capture::Mesh Mesh;alignas(Mesh) unsigned char storage[sizeof(Mesh)];
    auto make=[&](){Mesh* p=new(storage) Mesh(*Source(false));return std::shared_ptr<const Mesh>(p,[](const Mesh* p){p->~Mesh();});};
    auto first=make();kich_r4_capture::Batch batch;batch.native_models.push_back(Command(first));Require(h.models.Prepare(h.frame,h.level,batch)==0,"first allocation at fixed address");
    const auto vb=h.models.meshes.at(first.get()).vb;batch.native_models.clear();first.reset();auto replacement=make();batch.native_models.push_back(Command(replacement));
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.models.meshes.at(replacement.get()).vb!=vb && !f->objects.count(vb),"expired shared ownership prevents stale raw-pointer cache hit");
}
void AdmissionAndMaterial(){Harness h;Require(h.Start()==0,"budget setup");auto mesh=Source();kich_r4_capture::Batch batch;
    batch.native_models.assign(KICH_R4_MAX_MODEL_POSE_SLOTS,Command(mesh));const unsigned created=f->created;
    Require(h.models.Prepare(h.frame,h.level,batch)==KICH_R4_E_MEMORY && f->created==created && h.models.meshes.empty(),"actual 16MiB pose budget rejects before mesh/palette allocation");
    Require(KichR4ModelPaletteBytes(16)==1040 && KichR4ModelPaletteBytes(256)==16400,"allocation budget uses actual class including 16-byte header");
    h.frame.failure=0;batch.native_models.resize(1);mesh->texture="base";batch.native_models[0].texture.clear();
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0,"explicit empty selected material texture accepted");
    Require(model_draws.back().surface.textures[0]==h.white,"empty selected pass uses white and never silently restores mesh texture");
    h.models.ResetFrame();Require(h.Draw(batch,KichR4ModelSession::Geometry)<0,"reset frame cannot submit prior palettes");
}
void RetirementFailure(){Harness h;Require(h.Start()==0,"retirement setup");auto mesh=Source();kich_r4_capture::Batch batch;batch.native_models.push_back(Command(mesh));
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"retirement resource creation");const auto vb=h.models.meshes.at(mesh.get()).vb,ib=h.models.meshes.at(mesh.get()).ib;
    mesh.reset();batch.native_models.clear();f->fail=f->calls+2;Require(h.models.Prepare(h.frame,h.level,batch)<0 && !f->objects.count(vb) && f->objects.count(ib),"partial retirement keeps remaining ownership");
    f->fail=0;h.frame.failure=0;Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.models.meshes.empty() && !f->objects.count(ib) && !h.models.vertex_bytes && !h.models.index_bytes,"retry retirement never double releases completed vertex buffer");
}
void OrderedSingleSubmission(){Harness h;Require(h.Start()==0,"mixed order setup");auto mesh=Source();kich_r4_capture::Batch batch;
    auto a=Command(mesh),b=a,c=a;for(auto p:{&a,&b,&c}){p->style.blend=1;p->effect=6;}
    a.world.m[12]=1;b.world.m[12]=2;c.bounds.min[0]=100;c.bounds.max[0]=101;batch.native_models={a,b,c};
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"mixed order prepare");const unsigned uploaded=uploads;
    h.models.BeginPhase("mixed_transparent");const uint32_t order[]={1,0,2};
    for(unsigned i=0;i<3;++i)Require(h.models.Draw(h.frame,h.level,h.cb,h.base,batch,KichR4ModelSession::Forward,1,"mixed_transparent",NULL,0,order[i])==0,"single native command joins caller's global ordering");
    Require(model_draws.size()==2 && constants_drawn[0].world.m[12]==2 && constants_drawn[1].world.m[12]==1,"single command draw never re-sorts or replays other models");
    Require(h.models.phase_candidates==3 && h.models.phase_culled==1 && h.models.phase_selected==2 && h.models.phase_draws==2,"one mixed phase accumulates all selected native command observations");
    h.models.EndPhase();Require(!h.models.phase_active && uploads==uploaded,"phase boundary does not reupload per-frame palettes");
    Require(h.models.Draw(h.frame,h.level,h.cb,h.base,batch,KichR4ModelSession::Forward,1,"mixed_transparent",NULL,0,3)==KICH_R4_E_ARGUMENT,"single command index validated before submission");
}
void ResolvedTextureTelemetry(const char* directory){Harness h;Require(h.Start()==0,"resolved texture setup");h.level.checker=f->Add(KICH_R4_TEXTURE_2D);auto mesh=Source(false);
    kich_r4_capture::Batch batch;auto command=Command(mesh);command.texture="missing/model";batch.native_models.push_back(command);
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"resolved texture prepare");RenderSnapshotInfo info={};strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strncpy(info.directory,directory,sizeof(info.directory)-1);
    Require(RenderSnapshot_Request(),"resolved texture snapshot request");RenderSnapshot_Begin(info);
    Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && model_draws.back().surface.textures[0]==h.level.checker,"missing model texture actually binds checker");
    batch.native_models[0].texture.clear();Require(h.Draw(batch,KichR4ModelSession::Geometry)==0,"empty model texture binds white");RenderSnapshot_End("COMPLETE");
    std::ifstream file(RenderSnapshot_LastPath());std::ostringstream text;text<<file.rdbuf();
    Require(text.str().find("texture0=CHECKER_FALLBACK:missing/model")!=std::string::npos && text.str().find("texture0=$white")!=std::string::npos,"draw telemetry reports resolved fallback and white texture names");
}
void IdentityAndPoseTelemetry(const char* directory){Harness h;Require(h.Start()==0,"telemetry model setup");auto mesh=Source();kich_r4_capture::Batch batch;batch.engine_frame=51;
    auto a=Command(mesh),b=a,c=a,d=a;
    a.identity_measured=b.identity_measured=c.identity_measured=d.identity_measured=true;
    a.visual_id=1;b.visual_id=2;c.visual_id=3;d.visual_id=4;
    a.skeleton_id=b.skeleton_id=100;c.skeleton_id=200;d.skeleton_id=0;
    a.source_frame=b.source_frame=c.source_frame=d.source_frame=51;a.pose_hash=b.pose_hash=c.pose_hash=77;a.pose_hash_valid=b.pose_hash_valid=c.pose_hash_valid=true;
    batch.native_models={a,b,c,d};RenderSnapshotInfo info={};strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strncpy(info.directory,directory,sizeof(info.directory)-1);
    Require(RenderSnapshot_Request(),"identity snapshot request");RenderSnapshot_Begin(info);
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"identity telemetry prepares three unique uploads");
    Require(h.models.pose_uploads==3 && h.models.pose_reuses==1 && h.models.unique_skeletons==2 && h.models.unique_parts==4 && h.models.unknown_skeleton_commands==1,"commands and visual parts do not become four NPCs or skeletons");
    Require(h.models.unique_poses==2 && h.models.repeated_pose_uploads==0 && h.models.max_pose_upload_multiplicity==1,"shared skeleton pose parts reuse one upload; identical unrelated pose remains distinct");
    const unsigned before=uploads;Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && h.Draw(batch,KichR4ModelSession::Shadow)==0,"two phases use recorded palettes");
    Require(uploads==before && h.models.pose_uses==8 && h.models.command_uses[0]==2,"pass reuse never uploads a shared palette again");
    h.models.SnapshotSummary();RenderSnapshot_End("COMPLETE");std::ifstream file(RenderSnapshot_LastPath());std::ostringstream text;text<<file.rdbuf();
    Require(text.str().find("unique_skeleton_instances=2")!=std::string::npos && text.str().find("uploads_this_frame=1")!=std::string::npos,"schema records exact unique instance count and per-pose upload multiplicity");
    Require(text.str().find("successful_draw_uses=4")!=std::string::npos && text.str().find("NOT_NPC_ID")!=std::string::npos,"shared pose used by two parts in two phases remains explicitly not an NPC id");
}
void Rules(){KichR4ModelPose pose={};Require(ValidModelPose(pose),"rigid pose has empty palette");pose.skinned=1;Require(!ValidModelPose(pose),"skinned requires initialized nonempty palette");
    pose.bone_count=256;pose.skin_mode=2;Require(ValidModelPose(pose),"maximum palette accepted");pose.bone_count=257;Require(!ValidModelPose(pose),"palette overflow rejected");pose.bone_count=1;
    pose.bones[0][0]=std::numeric_limits<float>::quiet_NaN();Require(!ValidModelPose(pose),"nonfinite bone rejected");pose.bones[0][0]=1;pose.reserved=1;Require(!ValidModelPose(pose),"reserved ABI fields zero");
    Require(ValidModelRange(3,3,6) && !ValidModelRange(UINT32_MAX,3,6) && !ValidModelRange(0,UINT32_MAX,6) && !ValidModelRange(0,2,6),"overflow-safe triangle ranges");
    auto mesh=Source();Require(KichR4ModelSession::ValidSource(*mesh),"1W/2W immutable source accepted");mesh->vertices[0].weight=std::numeric_limits<float>::quiet_NaN();Require(!KichR4ModelSession::ValidSource(*mesh),"active 2W nonfinite weight rejected");
    mesh->vertices[0].bone[1]=0;Require(KichR4ModelSession::ValidSource(*mesh),"equal-bone 1W branch ignores unused weight");mesh->vertices[0].bone[0]=256;Require(!KichR4ModelSession::ValidSource(*mesh),"bone range validated before GPU upload");
    mesh=Source();auto cmd=Command(mesh);cmd.vertex_count=2;Require(!KichR4ModelSession::ValidCommand(cmd),"LOD prefix cannot reference missing vertex");cmd=Command(mesh);cmd.bones.pop_back();Require(!KichR4ModelSession::ValidCommand(cmd),"each instance supplies complete source palette");
    cmd=Command(mesh);mesh->hardware_weights_safe=false;Require(!KichR4ModelSession::ValidCommand(cmd),"unsafe hardware quantization rejected");cmd.skin_mode=KICH_R4_SKIN_SOFTWARE;Require(KichR4ModelSession::ValidCommand(cmd),"raw software mode retains extrapolating compatibility");
}
void PaletteClassesAndIsolation(){Harness h;Require(h.Start()==0,"palette variants start");kich_r4_capture::Batch batch;batch.engine_frame=93;
    auto mesh=Source();auto a=Command(mesh);a.identity_measured=true;a.skeleton_id=100;a.source_frame=93;a.pose_hash_valid=true;a.pose_hash=77;a.bounds_known=false;
    auto part=a;part.visual_id=2;part.world.m[12]=3;auto npc=a;npc.skeleton_id=101;
    auto changed=a;changed.bones[1].m[12]=7;auto raw=a;raw.skin_mode=KICH_R4_SKIN_SOFTWARE;
    auto unknown=a;unknown.identity_measured=false;auto stale=a;stale.source_frame=92;
    auto single_mesh=Source();single_mesh->two_weights=false;for(auto& v:single_mesh->vertices)v.bone[1]=v.bone[0];auto single=Command(single_mesh);
    single.identity_measured=true;single.skeleton_id=100;single.source_frame=93;
    batch.native_models={a,part,npc,changed,raw,unknown,stale,single};
    const uint32_t counts[]={16,17,32,33,64,65,128,129,256};
    uint64_t written=7*KichR4ModelPaletteBytes(16),logical=7*128,api_bytes=7*144;
    for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);++i){auto source=Source();source->bones=counts[i];source->vertices[0].bone[1]=static_cast<uint16_t>(counts[i]-1);
        auto c=Command(source);c.bones.back().m[12]=float(i+10);batch.native_models.push_back(c);
        written+=KichR4ModelPaletteBytes(KichR4ModelPaletteCapacity(counts[i]));logical+=counts[i]*64;api_bytes+=16+counts[i]*64;}
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"all capacity boundaries and reuse candidates accepted");
    Require(h.models.pose_reuses==1 && h.models.pose_uploads==16,"only same skeleton exact pose layout and mode reuse");
    Require(h.models.command_poses[0]==h.models.command_poses[1],"same skeleton parts share pose independently of world transform");
    std::set<KichR4Handle> distinct;for(size_t i=0;i<h.models.command_poses.size();++i)if(i!=1)Require(distinct.insert(h.models.command_poses[i]).second,"NPC, changed pose despite identical hash, raw mode, layout, unknown and stale identities do not alias");
    Require(h.models.pose_upload_bytes==written && h.models.pose_logical_bytes==logical && h.models.pose_api_bytes==api_bytes && h.models.pose_command_logical_bytes==logical+128,"logical payload, API range and mapped written class bytes measured separately");
    for(size_t i=0;i<batch.native_models.size();++i){const auto& c=batch.native_models[i];const Object& gpu=f->Find(h.models.command_poses[i]);const uint32_t capacity=KichR4ModelPaletteCapacity(static_cast<uint32_t>(c.bones.size()));
        Require(gpu.bytes.size()==KichR4ModelPaletteBytes(capacity),"smallest declared class covers addressed index span");
        Require(!memcmp(gpu.bytes.data()+16,c.bones.data(),c.bones.size()*64),"all addressed matrix bytes reach selected palette");
        for(size_t byte=16+c.bones.size()*64;byte<gpu.bytes.size();++byte)Require(!gpu.bytes[byte],"padding cannot expose stale palette matrices");}
    const unsigned before=uploads;Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && h.Draw(batch,KichR4ModelSession::Shadow)==0,"every palette class uses matching pipeline in camera and shadow");
    Require(uploads==before && constants_drawn[0].world.m[12]==0 && constants_drawn[1].world.m[12]==3,"shared palette passes keep separate world constants");
    const unsigned allocated=allocations;const auto handles=h.models.command_poses;
    ++batch.engine_frame;for(auto& c:batch.native_models)if(c.identity_measured)c.source_frame=batch.engine_frame;
    batch.native_models[0].bones[0].m[12]=9;batch.native_models[1].bones=batch.native_models[0].bones;
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && allocations==allocated,"warm storage reused without preserving prior-frame contents");
    Require(h.models.pose_uploads==16 && h.models.pose_reuses==1 && h.models.command_poses==handles,"frame reset revalidates aliases and uploads every unique pose");
    float translation=0;memcpy(&translation,f->Find(handles[0]).bytes.data()+16+12*sizeof(float),4);Require(translation==9,"warm WRITE_DISCARD payload replaces old pose");
    batch.native_models.clear();Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.models.command_poses.empty() && !h.models.pose_uploads && !h.models.pose_reuses,"empty frame invalidates palette associations");
}
void PalettePoolBudget(){Harness h;Require(h.Start()==0,"capacity pool budget setup");auto source=Source();source->bones=256;source->vertices[0].bone[1]=255;
    kich_r4_capture::Batch batch;const size_t large_count=(KICH_R4_MODEL_POSE_BUDGET_BYTES-KichR4ModelPaletteBytes(16))/KichR4ModelPaletteBytes(256);
    batch.native_models.assign(large_count,Command(source));Require(h.models.Prepare(h.frame,h.level,batch)==0,"largest class fills actual allocation budget");
    const auto old=h.models.command_poses.front();batch.native_models.assign(1024,Command(Source()));++batch.engine_frame;
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && !f->objects.count(old),"unused old class buffers retired when warm storage would exceed budget");
    Require(h.models.AllocatedPoseBytes()<=KICH_R4_MODEL_POSE_BUDGET_BYTES && h.models.pose_uploads==1024,"more than legacy 1023 commands fit small palette classes within exact budget");
}
}
int main(int argc,char** argv){try{(void)&Scenario;Require(argc==2,"snapshot output directory");Rules();const int positions=FailureScenario(0);for(int i=1;i<=positions;++i)FailureScenario(i);ReuseAndRouting();PointerReuse();AdmissionAndMaterial();RetirementFailure();OrderedSingleSubmission();ResolvedTextureTelemetry(argv[1]);IdentityAndPoseTelemetry(argv[1]);PaletteClassesAndIsolation();PalettePoolBudget();
    printf("G3c GPU model session: %d checks; %d injected failure positions passed\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
