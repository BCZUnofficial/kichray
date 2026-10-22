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
int main(){try{Models();HUD();SnapshotCapture();ABI();printf("B4.3b OGF / skinning / stream ownership / state validation: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
