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
int32_t ModelCompile(KichR4Handle,const KichR4ShaderDesc* d,const char*,uint32_t,KichR4Handle* out,char*,uint32_t){*out=0;const int32_t hr=f->Call("model shader");if(hr)return hr;
    Require(!strcmp(d->entry,"VSModelGPU"),"GPU model shader entry");*out=f->Add(KICH_R4_VERTEX_SHADER);return 0;}
int32_t ModelPipeline(KichR4Handle,const KichR4ModelPipelineDesc* d,KichR4Handle* out){*out=0;const int32_t hr=f->Call("model pipeline");if(hr)return hr;
    Require(d->struct_size==sizeof(*d) && f->Find(d->vs).kind==KICH_R4_VERTEX_SHADER && f->Find(d->ps).kind==KICH_R4_PIXEL_SHADER,"model pipeline binds explicit shader kinds");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);model_pipelines[*out]=*d;return 0;}
int32_t Pose(KichR4Handle,KichR4Handle* out){*out=0;const int32_t hr=f->Call("pose allocation");if(hr)return hr;
    *out=f->Add(KICH_R4_CONSTANT_BUFFER);f->Find(*out).bytes.resize(sizeof(KichR4ModelPose));++allocations;return 0;}
int32_t Upload(KichR4Handle,KichR4Handle h,const KichR4ModelPose* p){const int32_t hr=f->Call("pose upload");if(hr)return hr;
    Require(p && ValidModelPose(*p) && f->Find(h).bytes.size()==sizeof(*p),"validated complete palette upload");
    memcpy(f->Find(h).bytes.data(),p,sizeof(*p));initialized_poses.insert(h);++uploads;return 0;}
int32_t ModelUpdate(KichR4Handle,KichR4Handle h,const void* p,uint32_t bytes){const int32_t hr=f->Call("model constants");if(hr)return hr;
    Require(bytes==816 && f->Find(h).bytes.size()==bytes,"frame constants remain b0 816 bytes");memcpy(f->Find(h).bytes.data(),p,bytes);return 0;}
int32_t ModelRelease(KichR4Handle,KichR4Handle h){const int32_t hr=f->Call("model release");if(hr)return hr;
    Require(f->objects.erase(h)==1,"expired geometry releases exactly once");initialized_poses.erase(h);++f->released;return 0;}
int32_t ModelDraw(KichR4Handle,const KichR4ModelDrawDesc* d){const int32_t hr=f->Call("model draw");if(hr)return hr;
    const KichR4DrawIndexedDesc& draw=d->surface.state.draw;
    Require(d->struct_size==sizeof(*d) && ValidDeferredDraw(d->surface),"native state descriptor valid");
    const Object& vb=f->Find(draw.vertex_buffer);const Object& ib=f->Find(draw.index_buffer);
    Require(vb.stride==32 && ib.stride==2 && ValidDraw(draw,static_cast<uint32_t>(ib.bytes.size()),2),"immutable compact source vertices and uint16 indices");
    Require(initialized_poses.count(d->pose_buffer)==1,"draw uses initialized palette");
    Require(model_pipelines.count(draw.pipeline)==1,"model pipeline cannot become generic streamed pipeline");
    for(uint32_t i=0;i<draw.index_count;++i){uint16_t at;memcpy(&at,ib.bytes.data()+(draw.start_index+i)*2,2);Require(at<vb.bytes.size()/32,"LOD source index range");}
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
    Require(h.models.meshes.size()==2 && h.models.mesh_uploads==2 && h.models.vertex_bytes==192 && h.models.index_bytes==24,"clones share exactly one compact source allocation");
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
    KichR4ModelPose uploaded_a,uploaded_b;memcpy(&uploaded_a,f->Find(model_draws[0].pose_buffer).bytes.data(),sizeof(uploaded_a));memcpy(&uploaded_b,f->Find(model_draws[1].pose_buffer).bytes.data(),sizeof(uploaded_b));
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
    Require((KICH_R4_MAX_MODEL_POSE_SLOTS)*sizeof(KichR4ModelPose)<=KICH_R4_MODEL_POSE_BUDGET_BYTES && (KICH_R4_MAX_MODEL_POSE_SLOTS+1)*sizeof(KichR4ModelPose)>KICH_R4_MODEL_POSE_BUDGET_BYTES,"pose slot cap accounts full constant buffers including rigid slot");
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
void Rules(){KichR4ModelPose pose={};Require(ValidModelPose(pose),"rigid pose has empty palette");pose.skinned=1;Require(!ValidModelPose(pose),"skinned requires initialized nonempty palette");
    pose.bone_count=256;pose.skin_mode=2;Require(ValidModelPose(pose),"maximum palette accepted");pose.bone_count=257;Require(!ValidModelPose(pose),"palette overflow rejected");pose.bone_count=1;
    pose.bones[0][0]=std::numeric_limits<float>::quiet_NaN();Require(!ValidModelPose(pose),"nonfinite bone rejected");pose.bones[0][0]=1;pose.reserved=1;Require(!ValidModelPose(pose),"reserved ABI fields zero");
    Require(ValidModelRange(3,3,6) && !ValidModelRange(UINT32_MAX,3,6) && !ValidModelRange(0,UINT32_MAX,6) && !ValidModelRange(0,2,6),"overflow-safe triangle ranges");
    auto mesh=Source();Require(KichR4ModelSession::ValidSource(*mesh),"1W/2W immutable source accepted");mesh->vertices[0].weight=std::numeric_limits<float>::quiet_NaN();Require(!KichR4ModelSession::ValidSource(*mesh),"active 2W nonfinite weight rejected");
    mesh->vertices[0].bone[1]=0;Require(KichR4ModelSession::ValidSource(*mesh),"equal-bone 1W branch ignores unused weight");mesh->vertices[0].bone[0]=256;Require(!KichR4ModelSession::ValidSource(*mesh),"bone range validated before GPU upload");
    mesh=Source();auto cmd=Command(mesh);cmd.vertex_count=2;Require(!KichR4ModelSession::ValidCommand(cmd),"LOD prefix cannot reference missing vertex");cmd=Command(mesh);cmd.bones.pop_back();Require(!KichR4ModelSession::ValidCommand(cmd),"each instance supplies complete source palette");
    cmd=Command(mesh);mesh->hardware_weights_safe=false;Require(!KichR4ModelSession::ValidCommand(cmd),"unsafe hardware quantization rejected");cmd.skin_mode=KICH_R4_SKIN_SOFTWARE;Require(KichR4ModelSession::ValidCommand(cmd),"raw software mode retains extrapolating compatibility");
}
}
int main(int argc,char** argv){try{(void)&Scenario;Require(argc==2,"snapshot output directory");Rules();const int positions=FailureScenario(0);for(int i=1;i<=positions;++i)FailureScenario(i);ReuseAndRouting();PointerReuse();AdmissionAndMaterial();RetirementFailure();OrderedSingleSubmission();ResolvedTextureTelemetry(argv[1]);
    printf("G3b GPU model session: %d checks; %d injected failure positions passed\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
