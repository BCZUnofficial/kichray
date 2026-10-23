#include "../r4_capture/fixture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/KichR4Capture.h"
#include <limits>
#include <stdio.h>
#include <fstream>
#include <sstream>
using namespace kich_r4_capture;
namespace {
unsigned checks=0;
void Require(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
template<class F>void Reject(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Require(rejected,why);}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
bool Near(float a,float b){return fabsf(a-b)<1.e-4f;}
void NativeModes(){for(unsigned weights=0;weights<3;++weights){const fixture::Bytes raw=capture_fixture::Model(weights);std::shared_ptr<Mesh> mesh=Decode(View(raw));
    Require(mesh->shader=="models\\model","original OGF material name retained");
    Matrix world=kich_r4_scene::Identity(),vp=world,bones[2]={world,world};Style style;
    world.m[0]=-2;world.m[5]=3;world.m[12]=40;bones[0].m[12]=10;bones[1].m[13]=20;
    for(uint32_t mode=KICH_R4_SKIN_SOFTWARE;mode<=KICH_R4_SKIN_HARDWARE;++mode){
        Batch batch;batch.Begin(1280,720,1);batch.NativeModel(mesh.get(),mesh,world,vp,bones,2,3,3,1,style,mode,"model.ogf");
        Require(batch.vertices.empty() && batch.indices.empty() && batch.commands.empty(),"native models never expand CPU frame geometry");
        Require(batch.native_models.size()==1 && batch.models==1 && !batch.hands,"native instance recorded once");
        const NativeModelCommand cmd=batch.native_models[0];
        Require(cmd.mesh.get()==mesh.get() && cmd.first==3 && cmd.count==3 && cmd.vertex_count==3,"immutable source and progressive draw range retained");
        Require(cmd.texture=="base" && cmd.shader==mesh->shader && cmd.skin_mode==mode,"source material and exact weight policy retained");
        Require(cmd.bones.size()==(weights?2u:0u) && batch.pose_bytes==cmd.bones.size()*sizeof(Matrix),"only palette bytes retained per animated command");
        Require(cmd.bounds_known,"rigid and convex skin bounds available");
        Batch cpu;cpu.Begin(1280,720,1);cpu.Model(mesh.get(),*mesh,world,vp,bones,2,3,3,1,style,mode);
        for(size_t i=0;i<cpu.vertices.size();++i)for(unsigned a=0;a<3;++a)
            Require(cpu.vertices[i].world[a]>=cmd.bounds.min[a]-1.e-4f && cpu.vertices[i].world[a]<=cmd.bounds.max[a]+1.e-4f,"bounds contain production CPU skin reference under negative/nonuniform scale");
        batch.NativeModel(mesh.get(),mesh,world,vp,bones,2,3,3,1,style,mode,"model.ogf");Require(batch.native_models.size()==1,"same source instance and pass deduplicated");
        batch.NativeModel(mesh.get(),mesh,world,vp,bones,2,3,3,1,style,mode,"model.ogf","particle_distort","pfx\\pfx_distortion",NULL,5,4,0);
        Require(batch.native_models.size()==2 && batch.native_models[1].effect==5 && batch.native_models[1].shader_element==4,"separate shader element survives deduplication");
        style.max_z=.02f;vp.m[0]=1.75f;batch.NativeModel(mesh.get(),mesh,world,vp,bones,2,3,3,1,style,mode,"hands.ogf");
        Require(batch.native_models.back().layer==1 && Near(batch.native_models.back().vp.m[0],1.75f),"HUD projection and depth layer preserved");
        style.max_z=1;vp=kich_r4_scene::Identity();
        Reject([&](){batch.NativeModel(reinterpret_cast<void*>(9),mesh,world,vp,bones,2,2,3,1,style,mode,"bad.ogf");},"progressive vertex range validated before GPU");
        if(weights)Reject([&](){batch.NativeModel(mesh.get(),mesh,world,vp,bones,1,3,3,1,style,mode,"bad.ogf");},"insufficient palette rejected");
        Matrix invalid=world;invalid.m[2]=std::numeric_limits<float>::infinity();Reject([&](){batch.NativeModel(mesh.get(),mesh,invalid,vp,bones,2,3,3,1,style,mode,"bad.ogf");},"nonfinite transform rejected");
        batch.Begin(1280,720,2);Require(batch.native_models.empty() && batch.pose_bytes==0,"new frame clears instances and palette count");
    }
}}
void WeightBounds(){fixture::Bytes raw=capture_fixture::Model(2);Span v=Chunk(View(raw),3);const unsigned offset=static_cast<unsigned>(v.p-raw.data())+8+52;
    float weight=1.5f;uint32_t bits;memcpy(&bits,&weight,4);fixture::Set(raw,offset,bits);std::shared_ptr<Mesh> mesh=Decode(View(raw));
    Matrix world=kich_r4_scene::Identity(),bones[2]={world,world};bones[1].m[12]=20;Style style;Batch batch;batch.Begin(100,100,1);
    batch.NativeModel(mesh.get(),mesh,world,world,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"raw.ogf");Require(!batch.native_models[0].bounds_known,"raw extrapolating2W cannot be falsely culled by convex bounds");
    batch.NativeModel(mesh.get(),mesh,world,world,bones,2,3,3,1,style,KICH_R4_SKIN_HARDWARE,"hardware.ogf");Require(batch.native_models[1].bounds_known,"hardware clamped2W has conservative bounds");
    Require(Near(mesh->vertices[0].weight,1.5f),"weight policy does not mutate immutable source mesh");
    Reject([&](){batch.NativeModel(mesh.get(),mesh,world,world,bones,2,3,3,1,style,KICH_R4_SKIN_UNKNOWN,"bad.ogf");},"unknown2W policy rejected");
    fixture::Set(raw,offset,0x7fc00000u);Reject([&](){Decode(View(raw));},"active NaN weight rejected");
    fixture::Set(raw,static_cast<unsigned>(v.p-raw.data())+8,0);mesh=Decode(View(raw));Require(mesh->ignored_weights==1 && mesh->vertices[0].weight==0,"unused equal-bone NaN canonicalized safely");
}
void OwnershipAndRouting(){const fixture::Bytes raw=capture_fixture::Model(1);KichR4_CaptureEnable(true);KichR4_CaptureBeginFrame(800,600,1);
    State* state=KichR4_CaptureState();state->native_models=true;void* source=reinterpret_cast<void*>(1);void* copy=reinterpret_cast<void*>(2);
    KichR4_CaptureLoad(source,raw.data(),static_cast<uint32_t>(raw.size()),"shared.ogf");KichR4_CaptureCopy(copy,source);
    Matrix matrix=kich_r4_scene::Identity(),bones[2]={matrix,matrix};ModelInput in={};in.visual=copy;in.count=3;in.first=3;in.triangles=1;in.bone_count=2;in.bones=bones[0].m;in.skin_mode=KICH_R4_SKIN_SOFTWARE;
    memcpy(in.world,matrix.m,sizeof(in.world));memcpy(in.vp,matrix.m,sizeof(in.vp));
    KichR4_CaptureSubmitModel(in);Require(state->frame.native_models.size()==1 && state->frame.vertices.empty(),"native mode integration bypasses legacy CPU Model");
    std::weak_ptr<const Mesh> alive=state->frame.native_models[0].mesh;KichR4_CaptureForget(source);KichR4_CaptureForget(copy);
    Require(!alive.expired(),"frame retains source geometry after visual registry deletion");bones[1].m[12]=300;
    Require(state->frame.native_models[0].bones[1].m[12]==0,"palette owns data beyond caller scope");
    bones[1].m[12]=0;KichR4_CaptureLoad(copy,raw.data(),static_cast<uint32_t>(raw.size()),"replacement.ogf");KichR4_CaptureSubmitModel(in);
    Require(state->frame.native_models.size()==2 && state->frame.native_models[0].mesh!=state->frame.native_models[1].mesh,"reused visual address cannot deduplicate a replacement source mesh");
    in.style.scissor=true;in.style.rect[2]=400;in.style.rect[3]=300;KichR4_CaptureSubmitModel(in);
    Require(state->frame.native_models.size()==3 && state->frame.native_models.back().style.scissor,"distinct clipping state survives deduplication");
    KichR4_CaptureBeginFrame(800,600,2);Require(alive.expired(),"frame boundary releases last geometry owner");
    state->native_models=false;KichR4_CaptureLoad(copy,raw.data(),static_cast<uint32_t>(raw.size()),"legacy.ogf");KichR4_CaptureSubmitModel(in);
    Require(state->frame.native_models.empty() && state->frame.vertices.size()==3,"legacy route remains optin-independent");
    KichR4_CaptureEnable(false);
}
void SkeletonIdentity(){const fixture::Bytes raw=capture_fixture::Model(1);auto mesh=Decode(View(raw));Matrix m=kich_r4_scene::Identity(),bones[2]={m,m};Style style;Batch batch;batch.Begin(800,600,77);
    RenderSnapshotInfo info={};strcpy(info.directory,"/tmp");strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");Require(RenderSnapshot_Request(),"frontend identity snapshot request");RenderSnapshot_Begin(info);
    const void* skeleton=reinterpret_cast<void*>(100);batch.NativeModel(reinterpret_cast<void*>(1),mesh,m,m,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"part_a",NULL,NULL,NULL,0,UINT32_MAX,UINT32_MAX,skeleton);
    batch.NativeModel(reinterpret_cast<void*>(2),mesh,m,m,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"part_b",NULL,NULL,NULL,0,UINT32_MAX,UINT32_MAX,skeleton);
    Require(batch.native_models.size()==2 && batch.native_models[0].skeleton_id==100 && batch.native_models[1].skeleton_id==100 && batch.native_models[0].visual_id!=batch.native_models[1].visual_id,"actual parent identity shared by distinct visual parts");
    Require(batch.native_models[0].pose_hash==batch.native_models[1].pose_hash && batch.native_models[0].source_frame==77,"copied equal pose bytes have equal frame-local versions");
    bones[0].m[12]=1;batch.NativeModel(reinterpret_cast<void*>(3),mesh,m,m,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"part_c",NULL,NULL,NULL,0,UINT32_MAX,UINT32_MAX,skeleton);
    Require(batch.native_models[2].pose_hash!=batch.native_models[0].pose_hash,"changed copied palette changes pose version without altering engine animation");RenderSnapshot_End("COMPLETE");
    batch.Begin(800,600,78);batch.NativeModel(reinterpret_cast<void*>(1),mesh,m,m,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"off",NULL,NULL,NULL,0,UINT32_MAX,UINT32_MAX,skeleton);
    Require(batch.native_models[0].identity_measured && batch.native_models[0].skeleton_id==100 && batch.native_models[0].source_frame==78 && !batch.native_models[0].pose_hash_valid,"telemetry-off path retains cheap frame-local palette identity without hashing pose matrices");
    mesh=Decode(View(raw));batch.Begin(800,600,79);Require(RenderSnapshot_ProfileRequest(1),"minimal profile request");RenderSnapshot_Begin(info);
    batch.NativeModel(reinterpret_cast<void*>(1),mesh,m,m,bones,2,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"profile",NULL,NULL,NULL,0,UINT32_MAX,UINT32_MAX,skeleton);
    Require(batch.native_models[0].identity_measured && !batch.native_models[0].pose_hash_valid && !mesh->snapshot_hashed,"minimal profile preserves cheap identity but does not scan pose or source mesh");RenderSnapshot_End("COMPLETE");
}
void LightSelectionTelemetry(){State state;KichR4LocalLight light={};light.type=1;light.range=10;light.direction[2]=1;light.color[0]=1;
    for(uintptr_t i=1;i<=16;++i){const void* id=reinterpret_cast<const void*>(i);KichR4LightIdentity identity={i,100+i,KICH_R4_LIGHT_ROLE_RUNTIME,UINT32_MAX,UINT32_MAX,0};state.light_sources[id]=identity;
        KichR4LocalLight value=light;if(i==14)value.range=0;if(i==15)value.position[0]=1000;if(i==16)value.color[0]=0;state.Light(id,value,i!=13);}
    const float camera[]={0,0,0};KichR4LightingFrame selected={},after={};state.SelectLights(camera,selected);Require(selected.count==8,"existing top8 policy preserves distinct numerically identical lights");
    RenderSnapshotInfo info={};strcpy(info.directory,"/tmp");strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");Require(RenderSnapshot_Request(),"light exclusion telemetry request");RenderSnapshot_Begin(info);state.SnapshotLights(camera,selected);RenderSnapshot_End("COMPLETE");
    state.SelectLights(camera,after);Require(!memcmp(&selected,&after,sizeof(selected)),"light diagnostics do not reorder deduplicate or change selected descriptors");
    std::ifstream file(RenderSnapshot_LastPath());std::ostringstream text;text<<file.rdbuf();const std::string output=text.str();
    Require(output.find("registered_observed=16")!=std::string::npos && output.find("valid_active=14")!=std::string::npos && output.find("candidate=14")!=std::string::npos && output.find("admitted=12")!=std::string::npos && output.find("selected=8")!=std::string::npos,"complete observation active candidate admitted and selected counts recorded before dictionary limits");
    Require(output.find("selection_reason=INACTIVE")!=std::string::npos && output.find("selection_reason=INVALID_DESCRIPTOR")!=std::string::npos && output.find("selection_reason=OUTSIDE_DISTANCE_LIMIT")!=std::string::npos && output.find("selection_reason=NONPOSITIVE_COLOR")!=std::string::npos && output.find("selection_reason=BELOW_TOP_8_SCORE")!=std::string::npos,"every excluded source has its actual distinct selection reason");
    Require(output.find("controller=4294967295")==std::string::npos && output.find("controller_valid=0")!=std::string::npos,"unknown controller remains invalid absent value in registry dictionaries");
}
void LightIdentity(){KichR4_CaptureEnable(true);State* state=KichR4_CaptureState();const void* a=reinterpret_cast<void*>(1);const void* b=reinterpret_cast<void*>(2);
    KichR4LocalLight light={};light.range=10;light.type=1;light.color[0]=light.color[1]=light.color[2]=1;light.direction[2]=1;light.cone=1;
    KichR4LightIdentity first={1,10,KICH_R4_LIGHT_ROLE_LEVEL,3,4,KICH_R4_LIGHT_STATIC},second={2,11,KICH_R4_LIGHT_ROLE_RUNTIME,UINT32_MAX,UINT32_MAX,0};
    KichR4_CaptureLightIdentity(a,first);KichR4_CaptureLightIdentity(b,second);KichR4_CaptureLight(a,light,true);KichR4_CaptureLight(b,light,true);
    const float camera[]={0,0,0};KichR4LightingFrame frame={};KichR4_CaptureLighting(camera,frame,true);
    Require(frame.count==2 && frame.sources[0].serial!=frame.sources[1].serial,"equal numeric lights retain distinct source identities");
    KichR4_CaptureLighting(camera,frame,false);Require(frame.count==1 && frame.sources[0].serial==11,"capture API applies static policy without suppressing co-located runtime light");
    KichR4_CaptureLight(a,light,false);Require(state->light_observations.count(a) && !state->light_observations[a].active && !state->lights.count(a),"inactive source remains diagnostic but cannot illuminate");
    KichR4LocalLight invalid=light;invalid.range=0;KichR4_CaptureLight(a,invalid,true);Require(!state->light_observations[a].valid && !state->lights.count(a),"invalid descriptor retained only for exclusion reason");
    KichR4_CaptureLight(a,light,true);KichR4_CaptureLighting(camera,frame,true);
    Require(frame.sources[0].serial==10,"source serial survives deactivate/reactivate");KichR4_CaptureLightForget(a);
    Require(!state->light_sources.count(a) && !state->lights.count(a) && !state->light_observations.count(a),"destruction removes light source payload and diagnostics");
    bool registered=true;for(uintptr_t i=3;i<4098;++i){KichR4LightIdentity source=second;source.process_handle=i;source.serial=10000+i;
        if(!KichR4_CaptureLightIdentity(reinterpret_cast<const void*>(i),source))registered=false;}
    Require(registered && state->light_sources.size()==4096,"native identity registration reports bounded capacity accurately");
    Require(!KichR4_CaptureLightIdentity(reinterpret_cast<const void*>(5000),first) && state->light_identity_rejections==1,"identity overflow reports rejection before native light admission");
    Require(KichR4_CaptureLightIdentity(b,second),"existing source identity updates remain accepted at capacity");KichR4_CaptureEnable(false);
}
void StaticLightPolicy(){State state;KichR4LocalLight light={};light.type=1;light.range=10;light.direction[2]=1;light.color[0]=1;
    for(uintptr_t i=1;i<=17;++i){const void* id=reinterpret_cast<const void*>(i);const bool level=i<=9;
        KichR4LightIdentity source={i,200+i,level?KICH_R4_LIGHT_ROLE_LEVEL:KICH_R4_LIGHT_ROLE_RUNTIME,level?static_cast<uint32_t>(i):UINT32_MAX,0,level?static_cast<uint32_t>(KICH_R4_LIGHT_STATIC):0u};
        state.light_sources[id]=source;KichR4LocalLight value=light;if(level)value.color[0]=100;state.Light(id,value,true);}
    state.unshadowed_lights.insert(reinterpret_cast<const void*>(10));
    const float camera[]={0,0,0};KichR4LightingFrame off={},on={},again={};off.sun_color[0]=.75f;off.sun_direction[1]=-1;
    state.SelectLights(camera,off,false);Require(off.count==8 && off.r1_lights_requested==0 && off.r1_lights_applied==0,"R1LIGHTS off selects full runtime budget after static rejection");
    Require(off.sun_color[0]==.75f && off.sun_direction[1]==-1,"local policy leaves environment sun unchanged");
    for(unsigned i=0;i<8;++i)Require(off.sources[i].serial==210+i && off.sources[i].role==KICH_R4_LIGHT_ROLE_RUNTIME,"bright co-located static records never displace runtime sources when disabled");
    Require(off.shadow_mask==254,"shadow flags follow selected runtime slot identities after filtering");
    state.SelectLights(camera,on,true);Require(on.count==8 && on.r1_lights_requested==1 && on.r1_lights_applied==1,"R1LIGHTS on restores static eligibility before existing priority");
    for(unsigned i=0;i<8;++i)Require(on.sources[i].serial==201+i && on.sources[i].level_record==i+1,"equal level records remain distinct and preserve stable top8 order");
    Require(state.light_observations.size()==17 && state.light_sources.size()==17 && state.lights.size()==17,"policy toggle preserves complete active registration rather than disabling sources");
    again.sun_color[0]=.75f;again.sun_direction[1]=-1;state.SelectLights(camera,again,false);Require(!memcmp(&off,&again,sizeof(off)),"repeated policy toggle restores the same runtime set and slots");
    RenderSnapshotInfo info={};strcpy(info.directory,"/tmp");strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");Require(RenderSnapshot_Request(),"static policy telemetry request");RenderSnapshot_Begin(info);state.SnapshotLights(camera,off);RenderSnapshot_End("COMPLETE");
    std::ifstream file(RenderSnapshot_LastPath());std::ostringstream text;text<<file.rdbuf();const std::string output=text.str();
    Require(output.find("R2FLAG_R1LIGHTS_requested=0")!=std::string::npos && output.find("R2FLAG_R1LIGHTS_applied=0")!=std::string::npos,"telemetry records actual requested and applied toggle");
    Require(output.find("candidate=17")!=std::string::npos && output.find("policy_rejected=9")!=std::string::npos && output.find("admitted=8")!=std::string::npos && output.find("slots_used=8")!=std::string::npos,"policy telemetry separates pre-policy candidates rejected sources admitted sources and slots");
    Require(output.find("selected_LEVEL_RECORD=0")!=std::string::npos && output.find("selected_RUNTIME_REGISTERED=8")!=std::string::npos && output.find("selected_shadow_requested=7")!=std::string::npos,"selected origin and shadow request counts refer to runtime slots");
    Require(output.find("selection_reason=STATIC_LIGHT_DISABLED_R2FLAG_R1LIGHTS")!=std::string::npos && output.find("selected_slot=7")!=std::string::npos && output.find("static_source=1")!=std::string::npos,"per-source policy exclusion static semantics and selected slot are explicit");
}
}
int main(){try{NativeModes();WeightBounds();OwnershipAndRouting();SkeletonIdentity();LightIdentity();LightSelectionTelemetry();StaticLightPolicy();printf("G3c native model frontend: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
