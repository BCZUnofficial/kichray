#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
#include <limits>
static_assert(sizeof(kich_r4_lighting::Constants)==kich_r4::DeferredConstantsBytes && offsetof(kich_r4_lighting::Constants,alpha)==kich_r4::DeferredAlphaOffset,"provider alpha telemetry layout matches EXE constants");
namespace {
void Math(){
    using namespace kich_r4_scene;KichR4LocalLight light={};light.type=2;light.range=20;light.direction[2]=1;light.cone=1;
    KichR4LightProjector source;source.right[0]=1;auto m=kich_r4_projector::Projection(light,source);
    auto uv=[&](float x,float y,float z){float w=x*m.m[3]+y*m.m[7]+z*m.m[11]+m.m[15];return Vec3{(x*m.m[0]+y*m.m[4]+z*m.m[8]+m.m[12])/w*.5f+.5f,-(x*m.m[1]+y*m.m[5]+z*m.m[9]+m.m[13])/w*.5f+.5f,w};};
    auto center=uv(0,0,5),right=uv(1,0,5),up=uv(0,1,5);Require(center.x==.5f && center.y==.5f && center.z>0,"projector center projects to texture center");
    Require(right.x>.5f && up.y<.5f && uv(0,0,-1).z<0,"projection uses D3D texture V direction and excludes behind-light points");
    source.right[0]=0;source.right[1]=1;m=kich_r4_projector::Projection(light,source);right=uv(0,1,5);Require(right.x>.5f && std::fabs(right.y-.5f)<1.e-6f,"authored right vector rotates projector rather than following arbitrary shadow up");
    for(unsigned mode=0;mode<3;++mode){source.right[1]=0;source.right[2]=mode==1?1:0;source.right[0]=mode==2?std::numeric_limits<float>::quiet_NaN():0;
        m=kich_r4_projector::Projection(light,source);for(float value:m.m)Require(std::isfinite(value),"invalid/parallel right safely falls back to finite stable view");}
    struct Level{kich_r4_texture::Registry texture_registry;uint64_t checker=99;} level;
    source.texture="LIGHTS/torch.dds";auto& entry=level.texture_registry.entries["lights\\torch"];entry.faces=1;entry.handle=45;entry.planned=level.texture_registry.epoch;
    kich_r4_lighting::Constants c={};c.light_color[3]=2;uint64_t maps[]={11,12,13,14,15,16};
    kich_r4_projector::Configure(level,light,&source,c,maps);Require(kich_r4_projector::Enabled(c) && maps[0]==11 && maps[1]==45,"cookie uses spot spare slot without changing shadow");
    light.type=1;kich_r4_projector::Configure(level,light,&source,c,maps);Require(!kich_r4_projector::Enabled(c) && maps[1]==45 && maps[5]==16,"point disables projector without replacing any cube face");light.type=2;
    for(unsigned mode=0;mode<5;++mode){auto saved=entry;if(mode==0)entry.faces=6;if(mode==1)entry.handle=99;if(mode==2)entry.choice=UINT32_MAX;if(mode==3)entry.planned=0;if(mode==4)entry.handle=0;
        kich_r4_projector::Configure(level,light,&source,c,maps);Require(!kich_r4_projector::Enabled(c) && maps[1]==0 && maps[0]==11,"unavailable/budget/stale/cube/debug cookie keeps analytic spot");entry=saved;}
    // P0-S11 R2 s_accum_spot: a spot without a texture of its own projects lights\lights_spot01.
    KichR4LightProjector plain;kich_r4_projector::Configure(level,light,&plain,c,maps);
    Require(!kich_r4_projector::Enabled(c) && maps[1]==0,"without a resident lights_spot01 the plain spot stays analytic");
    auto& spot01=level.texture_registry.entries["lights\\lights_spot01"];spot01.faces=1;spot01.handle=46;spot01.planned=level.texture_registry.epoch;
    kich_r4_projector::Configure(level,light,&plain,c,maps);Require(kich_r4_projector::Enabled(c) && maps[1]==46,"a spot without a texture projects R2's lights_spot01");
    kich_r4_projector::Configure(level,light,NULL,c,maps);Require(kich_r4_projector::Enabled(c) && maps[1]==46,"so does a spot without a projector record");
    Require(KichR4SpotProjector("")=="lights\\lights_spot01" && KichR4SpotProjector("lights\\torch")=="lights\\torch","default projector name");
}
void Submission(bool available){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);
    if(available)f->files["$game_textures$/lights\\torch.dds"]=fixture::Dds(64,32);
    auto input=DeferredInput();Require(game.Render(input)==1,"projector setup");input.lighting.count=1;auto& spot=input.lighting.lights[0];
    spot.type=2;spot.range=20;spot.direction[2]=1;spot.color[0]=1;spot.cone=1;
    input.lighting.projectors[0].texture="lights\\torch";input.lighting.projectors[0].right[0]=1;
    kich_r4_capture::Batch batch;batch.Begin(input.width,input.height,0);game.level.texture_registry.Begin();
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,false)==0 && !game.level.texture_registry.entries.count("lights\\torch"),"menu does not request scene projector");
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,true)==0,"selected projector joins existing preflight");
    const auto& entry=game.level.texture_registry.entries.at("lights\\torch");Require(entry.wanted==game.level.texture_registry.epoch && entry.role==kich_r4_texture::LIGHTING,"cookie priority is lighting, not a separate unmanaged cache");
    Require(!input.lighting.CastsShadow(0),"projector test does not request local shadow");const auto opens=f->opens;const auto uploads=f->texture_calls;
    auto c=game.deferred.LocalConstants(spot);KichR4Handle maps[6]={};game.deferred.LocalInputs(game.level,input.lighting,0,c,maps,true);
    game.deferred.LocalInputs(game.level,input.lighting,0,c,maps);
    Require(game.deferred.frame_projector_spots==1 && game.deferred.frame_projector_named==1 && game.deferred.frame_projector_ready==unsigned(available) && game.deferred.frame_projector_fallback==unsigned(!available),"projector counters count selected spots once without forward/HUD duplicates");
    Require(kich_r4_projector::Enabled(c)==available && maps[1]==(available?entry.handle:0),"cookie works independently of shadow selection, missing DDS stays analytic");
    const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};Require(game.frame.BeginClear(clear)==0,"isolated local frame");
    Require(game.deferred.Pass(game.frame,game.level,input.width,input.height,1,game.deferred.light,0,0,0)==0 && game.deferred.Fullscreen(game.frame,game.level,KichR4DeferredSession::Local,c,game.deferred.normal,game.deferred.depth,maps,true)==0,"actual opaque local pass submission");
    Require(draw_entries.back()=="PSLocal" && draw_descriptions.back().textures[3]==maps[1] && !draw_descriptions.back().textures[2],"local cookie binds at t3 without a shadow dependency");
    if(available){const auto& sampler=draw_descriptions.back().state.surface.samplers[3];Require(sampler.u==3 && sampler.v==3 && sampler.min_filter==2 && sampler.mag_filter==2 && sampler.mip_filter==0,"actual projector pass uses clamp linear and no mip interpolation");}
    Require(f->opens==opens && f->texture_calls==uploads,"local pass does no late DDS reads or uploads");
    Require(game.frame.Present()==0,"projector frame completed");KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"projector handle follows existing level cleanup");
}
}
int main(){try{(void)&Scenario;Math();Submission(true);Submission(false);printf("E1.8 projector math/residency/real local binding: %d checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
