#include "fixture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/KichR4Capture.h"
#include "../../xr_3da/xrRender_R4/R4StreamRules.h"
#include <stdio.h>
#include <limits>
using namespace kich_r4_capture;
namespace {
int checks=0;void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
template<class F>void Reject(F action,const char* why){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Require(rejected,why);}
bool Near(float a,float b){return fabsf(a-b)<1.e-5f;}
fixture::Bytes BudgetModel(uint32_t count,uint32_t index_count){
    const fixture::Bytes base=capture_fixture::Model(1);const Span source=Chunk(View(base),3);
    fixture::Bytes record(source.p+8,source.p+8+60),vertices,indices,out;
    // A valid source TBN must survive the increased resident vertex stride.
    fixture::Set(record,20,0x3f800000u);fixture::Set(record,24,0x3f800000u);fixture::Set(record,40,0x3f800000u);
    vertices.reserve(8+size_t(count)*60);fixture::U32(vertices,0x12071980u);fixture::U32(vertices,count);
    for(uint32_t i=0;i<count;++i)vertices.insert(vertices.end(),record.begin(),record.end());
    indices.reserve(4+size_t(index_count)*2);fixture::U32(indices,index_count);
    for(uint32_t i=0;i<index_count;++i)fixture::U16(indices,static_cast<uint16_t>(i%3));
    for(uint32_t id=1;id<=2;++id){const Span data=Chunk(View(base),id);fixture::Chunk(out,id,fixture::Bytes(data.p,data.p+data.n));}
    fixture::Chunk(out,3,vertices);fixture::Chunk(out,4,indices);return out;
}
void ModelBudgets(){
    {const fixture::Bytes largest=BudgetModel(65536,3u*1024u*1024u);const std::shared_ptr<Mesh> mesh=Decode(View(largest));
        Require(mesh->Bytes()==10u*1024u*1024u,"maximum formerly 8MiB model fits expanded G3c vertex layout");
        Require(mesh->vertices.front().normal && mesh->vertices.front().tangent && mesh->vertices.front().binormal,"budget adjustment preserves source TBN");}
    const fixture::Bytes large=BudgetModel(65536,6),rigid=capture_fixture::Model(0);
    const uint64_t large_bytes=uint64_t(65536)*sizeof(SourceVertex)+12,rigid_bytes=3*sizeof(SourceVertex)+12;
    KichR4_CaptureEnable(true);State* state=KichR4_CaptureState();state->native_models=true;KichR4_CaptureBeginFrame(800,600,1);
    const uintptr_t count=static_cast<uintptr_t>((ModelRegistryBudgetBytes-rigid_bytes)/large_bytes);
    for(uintptr_t i=1;i<=count;++i){KichR4_CaptureLoad(reinterpret_cast<void*>(i),large.data(),static_cast<uint32_t>(large.size()),"prefetched.ogf");
        Require(state->models[reinterpret_cast<void*>(i)].mesh && state->models[reinterpret_cast<void*>(i)].error.empty(),"prefetch retains every model within expanded byte budget");}
    Require(state->ModelBytes()==count*large_bytes && state->ModelBytes()>96u*1024u*1024u,"preload crosses old 96MiB cap with exact resident accounting");
    void* base=reinterpret_cast<void*>(1);void* clone=reinterpret_cast<void*>(10000);void* decor=reinterpret_cast<void*>(10001);
    void* rendered=reinterpret_cast<void*>(10002);void* rejected=reinterpret_cast<void*>(10003);
    const Mesh* shared=state->models[base].mesh.get();KichR4_CaptureCopy(clone,base);KichR4_CaptureForget(base);
    Require(state->ModelBytes()==count*large_bytes && state->models[clone].mesh.get()==shared,"clone is counted once and retains mesh after base removal");
    KichR4_CaptureLoad(decor,rigid.data(),static_cast<uint32_t>(rigid.size()),"physics\\decor\\fire_vedro:1");KichR4_CaptureCopy(rendered,decor);
    Require(state->models[rendered].mesh && state->models[rendered].error.empty(),"visible rigid model remains available after large preload");
    uint32_t bones=99;Require(KichR4_CaptureModelNeeded(rendered,bones) && bones==0,"rigid model enters production frame route");
    const Matrix identity=kich_r4_scene::Identity();ModelInput input={};input.visual=rendered;input.count=3;input.first=3;input.triangles=1;
    memcpy(input.world,identity.m,sizeof(input.world));memcpy(input.vp,identity.m,sizeof(input.vp));
    KichR4_CaptureHUD(true);KichR4_CaptureHUD(false);KichR4_CaptureSubmitModel(input);
    Require(state->frame.Ready() && state->frame.native_models.size()==1 && state->frame.vertices.empty(),"preloaded save reaches a ready native frame without CPU expansion");
    const uint64_t used=state->ModelBytes();KichR4_CaptureLoad(rejected,large.data(),static_cast<uint32_t>(large.size()),"overflow.ogf");
    const Entry& failure=state->models[rejected];Require(!failure.mesh && !failure.error.empty() && state->ModelBytes()==used,"actual byte overflow drops candidate without damaging admitted meshes");
    Require(failure.error.find("used=")!=std::string::npos && failure.error.find("requested=")!=std::string::npos && failure.error.find("limit=")!=std::string::npos,"budget error reports used requested and limit bytes");
    Require(state->models[clone].mesh.get()==shared && state->frame.Ready(),"failed preload leaves clone and prepared frame intact");
    KichR4_CaptureForget(clone);Require(state->ModelBytes()==used-large_bytes,"last registry owner releases its payload allowance");
    KichR4_CaptureLoad(rejected,large.data(),static_cast<uint32_t>(large.size()),"replacement.ogf");
    Require(state->models[rejected].mesh && state->models[rejected].error.empty() && state->ModelBytes()==used,"released allowance admits replacement and clears prior rejection");
    const size_t registered=state->models.size();KichR4_CaptureBeginFrame(800,600,2);
    Require(state->frame.native_models.empty() && state->models.size()==registered && state->ModelBytes()==used,"frame reset releases commands while preserving preload registry");
    KichR4_CaptureEnable(false);
}
void Models(){for(unsigned weights=0;weights<3;++weights){const fixture::Bytes raw=capture_fixture::Model(weights);const std::shared_ptr<Mesh> mesh=Decode(View(raw));
    Require(mesh && mesh->vertices.size()==3 && mesh->indices.size()==6 && mesh->texture=="base","inline OGF positions/UV/material decoded");
    Require(mesh->skinned==(weights!=0) && (!weights || mesh->bones==2),"1W/2W bone IDs retained");
    Batch batch;batch.Begin(1920,1080,3);Matrix world=kich_r4_scene::Identity(),vp=world,bones[2]={world,world};
    world.m[12]=4;bones[0].m[12]=10;bones[1].m[12]=20;Style s;
    batch.Model(mesh.get(),*mesh,world,vp,bones,2,3,3,1,s);
    const float expected=weights==1?24.f:weights==2?16.5f:4.f;
    Require(Near(batch.vertices[0].p[0],expected) && Near(batch.vertices[2].uv[0],1),"skin pose then world transform; correct 2W weight direction");
    Require(batch.models==1 && !batch.hands && batch.indices[2]==2,"actual progressive index offset");
    batch.Model(mesh.get(),*mesh,world,vp,bones,2,3,3,1,s);Require(batch.models==1,"legacy multipass does not duplicate model");
    s.max_z=.02f;batch.Model(mesh.get(),*mesh,world,vp,bones,2,3,3,1,s);Require(batch.hands==1,"weapon near-depth layer separated");
    Reject([&](){batch.Model(reinterpret_cast<void*>(1),*mesh,world,vp,bones,2,2,3,1,s);},"LOD vertex bounds checked");
    if(weights)Reject([&](){batch.Model(mesh.get(),*mesh,world,vp,bones,1,3,3,1,s);},"insufficient bone pose rejected");
    for(size_t n=0;n<raw.size();++n){fixture::Bytes short_file(raw.begin(),raw.begin()+n);Reject([&](){Decode(View(short_file));},"every truncated OGF rejected");}
    State state;state.Load(reinterpret_cast<void*>(1),View(raw));state.Copy(reinterpret_cast<void*>(2),reinterpret_cast<void*>(1));
    Require(state.ModelBytes()==mesh->Bytes() && state.models.size()==2,"model clones share immutable geometry");state.models.erase(reinterpret_cast<void*>(1));
    Require(state.models.begin()->second.mesh && state.ModelBytes()==mesh->Bytes(),"clone retains source after original destruction");state.models.clear();Require(!state.ModelBytes(),"last model reference releases CPU geometry");
    }
    fixture::Bytes container=fixture::Visual(0,1,0,0,3);Require(!Decode(View(container)),"level container skipped to avoid static duplicates");
    fixture::Bytes bad=capture_fixture::Model(2);const Span v=Chunk(View(bad),3);fixture::Set(bad,static_cast<unsigned>(v.p-bad.data())+8,0xffffffffu);
    Reject([&](){Decode(View(bad));},"bone IDs bounded");
    // P0-S10 X-Ray find_chunk: the first match wins and the scan stops there.
    {const fixture::Bytes raw=capture_fixture::Model(1);
        // actors\neytral\stalker_neytral_balon_1: trailing chunk 19 claims 56 bytes, 52 remain.
        fixture::Bytes tail=raw;fixture::U32(tail,19);fixture::U32(tail,56);tail.resize(tail.size()+52,0);
        const std::shared_ptr<Mesh> repaired=Decode(View(tail));Require(repaired && repaired->indices.size()==6,"a damaged record after every model chunk is never read");
        Reject([&](){FirstChunk(View(tail),19,false);},"the damaged record itself is rejected when requested");
        fixture::Bytes partial=raw;fixture::U32(partial,7);partial.resize(partial.size()+3,0);
        Require(Decode(View(partial)) && FirstChunk(View(partial),7,false).p==NULL,"a trailing header shorter than 8 bytes ends the search");
        // A duplicate after the first: X-Ray reads the first one.
        fixture::Bytes twice=raw;fixture::Bytes texture;fixture::Z(texture,"OTHER.dds");fixture::Chunk(twice,2,texture);
        const std::shared_ptr<Mesh> first=Decode(View(twice));Require(first && first->texture=="base","duplicate chunk: the first one is used");
        Reject([&](){Chunk(View(twice),2);},"level data keeps its strict duplicate check");
        // A damaged record before a required chunk: not found, the model is rejected.
        fixture::Bytes broken;fixture::U32(broken,99);fixture::U32(broken,100000);broken.insert(broken.end(),raw.begin(),raw.end());
        Reject([&](){Decode(View(broken));},"a damaged record before the header rejects the model");
        // The requested chunk itself must be complete.
        fixture::Bytes cut(raw.begin(),raw.end()-1);Reject([&](){FirstChunk(View(cut),4);},"a truncated requested chunk is rejected");}
}
void HUD(){Batch b;b.Begin(1920,1080,9);Vertex v=b.Screen(0,0,.0001f,2,1,.5f,0x80402010u);
    Require(Near(v.p[0],(-1.f+1.f/1920)*.5f) && Near(v.p[1],(1.f-1.f/1080)*.5f) && Near(v.p[3],.5f),"DX9 pixels/RHW mapped to DX11 clip space");
    Require(v.color==0x80102040u,"D3DCOLOR ARGB converted to RGBA bytes");
    Reject([&](){b.Screen(1,1,1,0,0,0,0);},"zero RHW rejected");
    Reject([&](){b.Screen(std::numeric_limits<float>::quiet_NaN(),1,1,1,0,0,0);},"nonfinite screen position rejected");
    Buffer buffer;unsigned char raw[]={0,1,2,3,4,5,6,7};buffer.Put(raw,12,8);raw[2]=99;Require(buffer.Read(14,2)[0]==2,"borrowed stream copied before unlock");
    Reject([&](){buffer.Read(10,4);},"unwritten buffer data rejected");
    State state;state.Begin(800,600,5);state.frame.ui_scope=1;void* key=reinterpret_cast<void*>(1);
    state.Lock(key,raw,32,8,0);state.Unlock(key,4);Require(state.buffers[key].Read(32,4)[2]==99,"actual written count captured");
    Reject([&](){state.buffers[key].Read(36,1);},"unused locked vertices remain inaccessible");
    state.Lock(key,raw,0,8,1);state.Unlock(key,8);Reject([&](){state.buffers[key].Read(32,1);},"DISCARD invalidates old stream ranges");
    state.Begin(800,600,6);Require(state.buffers[key].segments.empty(),"frame boundary drops transient stream copies");
    KichR4_CaptureEnable(true);KichR4_CaptureBeginFrame(800,600,1);KichR4_CaptureHUD(true);KichR4_CaptureLock(key,raw,0,8,1);
    KichR4_CaptureUnlock(key,9);Require(!KichR4_CapturedFrame()->error.empty(),"capture failure is contained and reported");
    KichR4_CaptureBeginFrame(800,600,2);KichR4_CaptureHUD(false);Require(!KichR4_CapturedFrame()->Ready(),"HUD imbalance cannot hide a legacy frame");
    KichR4_CaptureEnable(false);Require(!KichR4_CapturedFrame() && !KichR4_CaptureActive(),"capture shutdown releases registry");
}
void SnapshotCapture(){const fixture::Bytes raw=capture_fixture::Model(0);const std::shared_ptr<Mesh> mesh=Decode(View(raw));
    Batch batch;Matrix world=kich_r4_scene::Identity();Style style;batch.Begin(800,600,1);
    batch.Model(mesh.get(),*mesh,world,world,NULL,0,3,0,1,style);
    Require(!batch.commands[0].provenance && !mesh->snapshot_hashed,"ordinary frame allocates no diagnostic payload and does not hash models");
    RenderSnapshotInfo info={};strcpy(info.directory,".");strcpy(info.renderer,"R4");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("CPU_CAPTURE");
    batch.Begin(800,600,2);batch.Model(mesh.get(),*mesh,world,world,NULL,0,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"model.ogf",true);
    Require(batch.commands[0].provenance.get()!=NULL && mesh->snapshot_hashed,"snapshot owns diagnostic provenance");
    const RenderSnapshotProvenance first=*batch.commands[0].provenance;
    Require(!strcmp(first.semantic_type,"RIGID_MODEL") && first.uv_count==3 && Near(first.uv[4],1),"model semantics and actual distinct indexed UVs");
    world.m[12]=20;batch.Begin(800,600,3);batch.Model(mesh.get(),*mesh,world,world,NULL,0,3,3,1,style,KICH_R4_SKIN_SOFTWARE,"model.ogf",true);
    Require(!strcmp(first.mesh_id,batch.commands[0].provenance->mesh_id),"source mesh ID survives transforms and frame changes");
    style.src=2;batch.Begin(800,600,4);batch.Model(mesh.get(),*mesh,world,world,NULL,0,3,0,1,style,KICH_R4_SKIN_SOFTWARE,"model.ogf",true);
    Require(strcmp(first.mesh_id,batch.commands[0].provenance->mesh_id)!=0 && strcmp(first.material_id,batch.commands[0].provenance->material_id)!=0,"mesh range and material state distinguish IDs");
    const float uv[]={-1,0,2,1,3,2,4,3,5,4};RenderSnapshot_Object("PARTICLE","effect:test","material:test","CParticleEffect",RENDER_SNAPSHOT_PARTICLE);
    RenderSnapshot_UV(uv,5,"PARTICLE_SOURCE_UV");Command cmd=batch.commands[0];SnapshotCommand(cmd,batch.vertices,batch.indices,"UNKNOWN_STREAM");
    RenderSnapshot_Context("OTHER","other","other");
    Require(cmd.provenance->flags==RENDER_SNAPSHOT_PARTICLE && cmd.provenance->uv_count==4 && Near(cmd.provenance->uv[0],-1) && !strcmp(cmd.provenance->mesh_id,"effect:test"),"deferred command preserves particle source after global context changes");
    RenderSnapshot_End("COMPLETE");remove(RenderSnapshot_LastPath());
    batch.Begin(800,600,5);Require(batch.commands.empty(),"next frame releases command provenance");
}
void ABI(){KichR4StreamDrawDesc d={};d.struct_size=sizeof(d);d.draw.struct_size=sizeof(d.draw);d.texture=1;d.source_blend=5;d.dest_blend=6;d.max_depth=1;
    Require(kich_r4::ValidStreamDraw(d),"valid additive stream descriptor");
    for(unsigned i=0;i<12;++i){KichR4StreamDrawDesc bad=d;switch(i){case 0:bad.struct_size=0;break;case 1:bad.source_blend=0;break;case 2:bad.dest_blend=11;break;
        case 3:bad.blend_enable=2;break;case 4:bad.scissor_enable=2;break;case 5:bad.min_depth=-1;break;case 6:bad.max_depth=2;break;case 7:bad.depth_write=2;break;
        case 8:bad.min_depth=std::numeric_limits<float>::quiet_NaN();break;case 9:bad.max_depth=std::numeric_limits<float>::infinity();break;
        case 10:bad.scissor_enable=1;bad.scissor[2]=-1;break;case 11:bad.texture=0;break;}Require(!kich_r4::ValidStreamDraw(bad),"invalid stream state rejected before GPU calls");}
}
}
// P0-S10 r4_lamp_fade against R2 light::get_LOD written out independently:
// g_fSCREEN = w*h*(90/fov)^2*(EPS_S+r__LOD), r_ssaGLOD_* = (ssa/3)^2/g_fSCREEN.
double ReferenceLOD(double radius,double distance_squared,double fade,double w,double h,double fov,double lod,double start,double end){
    const double screen=w*h*(90/fov)*(90/fov)*(.0000001+lod),s=(start/3)*(start/3)/screen,e=(end/3)*(end/3)/screen;
    const double ssa=fade*radius/(distance_squared+1e-6),t=(ssa-e)/(s-e);return std::sqrt(t<0?0:t>1?1:t);}
void LampFade(){
    float window[3];KichR4LampFadeWindow(window,1,1920,1080,67.5f,1,256,64);
    Require(window[0]==1 && window[1]>window[2] && window[2]>0,"fade window from r__geometry_lod");
    const float camera[3]={0,0,0};KichR4LocalLight point={};point.type=1;point.range=10;point.color[0]=point.color[1]=point.color[2]=1;
    for(float d:{1.f,30.f,60.f,90.f,150.f,240.f,400.f}){point.position[0]=d;
        Require(std::fabs(KichR4LampLOD(point,camera,window)-ReferenceLOD(10,double(d)*d,1,1920,1080,67.5,1,256,64))<1e-4,"point lamp LOD follows the R2 measure");}
    point.position[0]=20;Require(KichR4LampLOD(point,camera,window)==1,"a near lamp keeps full colour");
    point.position[0]=400;Require(KichR4LampLOD(point,camera,window)<=.001f,"a distant lamp falls below the cut");
    // Spot spheres as light::spatial_move: acute cone R=range/(2cos^2(cone/2)) along the axis; obtuse range*tan(cone/2) at range.
    KichR4LocalLight spot=point;spot.type=2;spot.position[0]=0;spot.direction[2]=2;spot.cone=1.0471976f;float c[3],r;
    KichR4LightSphere(spot,c,r);Require(std::fabs(r-10/1.5f)<1e-4f && std::fabs(c[2]-10/1.5f)<1e-4f && c[0]==0,"acute spot sphere");
    spot.cone=2.0943951f;KichR4LightSphere(spot,c,r);Require(std::fabs(r-10*std::tan(1.0471976f))<1e-3f && std::fabs(c[2]-10)<1e-4f,"obtuse spot sphere");
    float off[3]={0,0,0};Require(KichR4LampLOD(point,camera,off)==1,"r4_lamp_fade 0 keeps every lamp");
    KichR4LampFadeWindow(off,0,1920,1080,67.5f,1,256,64);Require(off[0]==0,"zero fade disables the window");
    // Selection: shadowed lamps fade or drop, unshadowed ones keep their colour.
    State state;KichR4LocalLight near_lamp=point,mid=point,far_lamp=point,plain=point;
    near_lamp.position[0]=20;mid.position[0]=150;far_lamp.position[0]=400;plain.position[0]=400;plain.position[1]=1;
    state.Light(reinterpret_cast<void*>(1),near_lamp,true);state.Light(reinterpret_cast<void*>(2),mid,true);
    state.Light(reinterpret_cast<void*>(3),far_lamp,true);state.Light(reinterpret_cast<void*>(4),plain,true);
    state.unshadowed_lights.insert(reinterpret_cast<void*>(4));
    KichR4LightingFrame frame;memcpy(frame.lamp_fade,window,sizeof(window));state.SelectLights(camera,frame);
    Require(frame.count==3 && frame.lamp_faded==1,"the shadowed lamp below LOD 0.001 is dropped");
    const float expected=float(ReferenceLOD(10,150.*150,1,1920,1080,67.5,1,256,64));Require(expected>.05f && expected<.95f,"mid lamp lies inside the fade window");
    bool near_seen=false,mid_seen=false,plain_seen=false;
    for(unsigned i=0;i<frame.count;++i){const KichR4LocalLight& l=frame.lights[i];
        if(l.position[0]==20){near_seen=l.color[0]==1;}
        if(l.position[0]==150){mid_seen=std::fabs(l.color[0]-expected)<1e-4f && std::fabs(l.color[2]-expected)<1e-4f;}
        if(l.position[1]==1){plain_seen=l.color[0]==1 && !frame.CastsShadow(i);}}
    Require(near_seen && mid_seen && plain_seen,"near full, mid scaled by LOD, unshadowed untouched");
    KichR4LightingFrame direct;state.SelectLights(camera,direct);Require(direct.count==4 && !direct.lamp_faded,"no window (direct/test producers): no fade");
}
// P0-S11 r4_npc_torch_shadows 0: an NPC torch (torch projector, unshadowed
// omni twin at its exact position) loses its shadow map; the actor's torch (no
// twin) and a hanging lamp spot with its ambient point (own texture) keep theirs.
void NpcTorches(){
    State state;const float camera[3]={0,0,0};
    KichR4LocalLight spot={};spot.type=2;spot.range=13;spot.direction[2]=1;spot.cone=1.0471976f;spot.color[0]=spot.color[1]=spot.color[2]=1.2f;
    KichR4LocalLight omni={};omni.type=1;omni.range=.75f;omni.color[0]=omni.color[1]=omni.color[2]=.5f;
    auto put=[&](uintptr_t id,KichR4LocalLight l,float x,const char* texture,bool shadow){l.position[0]=x;l.position[1]=1.7f;
        void* key=reinterpret_cast<void*>(id);state.Light(key,l,true);if(texture)state.light_projectors[key].texture=texture;if(!shadow)state.unshadowed_lights.insert(key);};
    put(1,spot,15,"internal\\internal_light_torch_r2",true);put(2,omni,15,NULL,false);       // NPC torch + light_omni
    put(3,spot,.3f,"Internal\\Internal_Light_Torch_R2",true);                                // actor: light_omni inactive
    put(4,spot,20,"internal\\internal_light_attenuated",true);put(5,omni,20,NULL,false);    // hanging lamp + ambient point
    put(6,spot,25,"internal\\internal_light_torch_r2",true);put(7,omni,25.001f,NULL,false); // a point that is not its twin
    auto shadowed=[&](const KichR4LightingFrame& f,float x){for(unsigned i=0;i<f.count;++i)if(f.lights[i].position[0]==x && f.lights[i].type==2)return f.CastsShadow(i);
        throw std::runtime_error("spot missing from the frame");};
    KichR4LightingFrame keep;state.SelectLights(camera,keep);
    Require(keep.count==7 && shadowed(keep,15) && shadowed(keep,.3f) && shadowed(keep,20) && shadowed(keep,25),"direct/test producers keep every requested shadow");
    KichR4LightingFrame off;off.npc_torch_shadows=0;state.SelectLights(camera,off);
    Require(off.count==7 && !shadowed(off,15),"r4_npc_torch_shadows 0: the NPC torch lights without a shadow map");
    Require(shadowed(off,.3f),"the actor's torch has no omni twin and keeps its shadow");
    Require(shadowed(off,20),"a hanging lamp spot with its ambient point keeps its shadow (own texture)");
    Require(shadowed(off,25),"only an exact position match is a twin");
    for(unsigned i=0;i<off.count;++i)if(off.lights[i].position[0]==15 && off.lights[i].type==2)Require(off.lights[i].color[0]==1.2f,"the NPC torch keeps its full light");
    Require(KichR4TorchProjector("internal\\internal_light_torch_r2") && !KichR4TorchProjector("internal\\internal_light_attenuated") && !KichR4TorchProjector(""),"torch projector by its texture");
}
int main(){try{Models();ModelBudgets();HUD();SnapshotCapture();ABI();LampFade();NpcTorches();printf("B4.3b OGF / skinning / stream ownership / state validation: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
