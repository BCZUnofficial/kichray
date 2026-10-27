// P0-S11 PERF02 r4_sun_far_interval: the reuse rule alone, then the real pass
// planner over consecutive frames (recording API, no GPU pixels).
#include "recording.h"
#include "../../xr_3da/KichR4SunFarReuse.h"
#include "../../xr_3da/KichR4SubmitProfile.h"
#include <limits>
namespace {
kich_r4_scene::Matrix Ortho(float scale,float shear=0){kich_r4_scene::Matrix m=kich_r4_scene::Identity();
    m.m[0]=m.m[5]=scale;m.m[10]=scale*.25f;m.m[8]=shear;m.m[12]=3;m.m[13]=-4;return m;}
void Rule(){
    using namespace kich_r4_sun_far;
    const Key key={7,1,8192,3,0};const float camera[3]={10,2,-5};const kich_r4_scene::Matrix view=Ortho(2.f/512);
    State state;Require(!Reuse(state,2,101,camera,view,key),"nothing rendered yet");
    state.Rendered(100,camera,view,key);
    Require(Reuse(state,2,101,camera,view,key),"interval 2 keeps the next frame");
    Require(!Reuse(state,2,102,camera,view,key),"interval 2 renders every second frame");
    Require(Reuse(state,4,103,camera,view,key) && !Reuse(state,4,104,camera,view,key),"interval 4 keeps three frames");
    Require(!Reuse(state,1,101,camera,view,key),"interval 1 renders every frame");
    Require(!Reuse(state,2,99,camera,view,key),"a frame counter that went back renders");
    Key other=key;other.map=8;Require(!Reuse(state,2,101,camera,view,other),"another map");
    other=key;other.generation=2;Require(!Reuse(state,2,101,camera,view,other),"a recreated map");
    other=key;other.size=4096;Require(!Reuse(state,2,101,camera,view,other),"another resolution");
    other=key;other.revision=4;Require(!Reuse(state,2,101,camera,view,other),"another level revision");
    other=key;other.cull=2;Require(!Reuse(state,2,101,camera,view,other),"another sun cull mode");
    float moved[3]={10.49f,2,-5};Require(Reuse(state,2,101,moved,view,key),"inside the 0.5 m drift");
    moved[0]=10.3f;moved[1]=2.3f;moved[2]=-4.7f;Require(!Reuse(state,2,101,moved,view,key),"0.52 m of drift renders (hole slack)");
    kich_r4_scene::Matrix turned=view;turned.m[8]+=view.m[0]*.5e-3f;Require(Reuse(state,2,101,camera,turned,key),"sun drift inside 1e-3 of the scale");
    turned.m[8]=view.m[0]*2.e-3f;Require(!Reuse(state,2,101,camera,turned,key),"a turned light basis (lightning) renders");
    kich_r4_scene::Matrix shifted=view;shifted.m[12]+=.5f;shifted.m[13]-=.25f;Require(Reuse(state,2,101,camera,shifted,key),"translation is the drift rule's, not the turn rule's");
    turned=view;turned.m[5]=std::numeric_limits<float>::quiet_NaN();Require(!Reuse(state,2,101,camera,turned,key),"nonfinite matrix renders");
    state.Clear();Require(!Reuse(state,2,101,camera,view,key),"a cleared state (failed render) renders");
}
void SubmitStages(){
    using namespace kich_r4_submit;kich_r4_submit::Profile& p=Global();p.Reset();
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(50);
    p.Begin(true);p.Add(Plan,1);p.Add(Preflight,2);p.Add(SunFar,3);p.Count(SunFarReused);p.Finish(10);
    Require(p.frames==1 && p.sum[Other]==4 && p.peak[SunFar]==3 && p.count_sum[SunFarReused]==1,"the unattributed rest of the submit is other");
    p.Begin(true);p.Add(Plan,12);p.Finish(10);Require(p.frames==2 && p.sum[Other]==4,"stages over the total leave other at zero");
    p.Begin(true);p.Add(Plan,5);p.Cancel();p.Finish(10);Require(p.frames==2,"a cancelled frame is not counted");
    p.Begin(false);p.Add(Plan,5);p.Finish(10);Require(p.frames==2 && p.sum[Plan]==13,"unmeasured frames add nothing");
    {p.Begin(true);Laps laps;laps.Lap(Setup);laps.Lap(GBuffer);p.Finish(1000);Require(p.sum[Setup]>=0 && p.sum[GBuffer]>=0 && p.frames==3,"laps feed consecutive stages");}
    p.Reset();kich_r4_record::Request(false);kich_r4_record::BeginFrame(51);
}
struct Frame {size_t far_passes,far_draws;bool far_bound;kich_r4_scene::Matrix far_view;uint32_t reused;bool record_completed;kich_r4_scene::Matrix record;};
// One native frame; counts the depth passes into the 512 m map and whether a
// lighting draw samples it.
Frame Render(kich_r4_game::Session& game,KichR4GameFrame& input,uint32_t frame){
    input.engine_frame=frame;const size_t pass_first=passes.size(),draw_first=draw_descriptions.size();
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(frame);
    Require(game.Render(input)==1,"native frame submitted");
    Frame out={0,0,false,game.deferred.base.shadow[1],game.deferred.frame_sun_far_reused,game.deferred.shadow_record.sun[1].completed,{}};
    memcpy(out.record.m,game.deferred.shadow_record.sun[1].matrix,64);
    const KichR4Handle far_map=game.deferred.sun_far_map;Require(far_map!=0,"far map exists");
    for(size_t i=pass_first;i<passes.size();++i)if(passes[i].depth==far_map && !passes[i].color_count)++out.far_passes;
    for(size_t i=draw_first;i<draw_descriptions.size();++i)for(unsigned t=0;t<KICH_R4_DEFERRED_TEXTURE_SLOTS;++t)if(draw_descriptions[i].textures[t]==far_map)out.far_bound=true;
    kich_r4_record::Request(false);return out;
}
void Planner(){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(DeferredAPIs(),files,"l01_escape",NULL);
    game.deferred.native_mode=true;KichR4GameFrame input=DeferredInput();
    input.lighting.sun_direction[0]=.3f;input.lighting.sun_direction[1]=-.9f;input.lighting.sun_direction[2]=.2f;
    input.lighting.sun_far_interval=2;
    const Frame a=Render(game,input,10),b=Render(game,input,11),c=Render(game,input,12);
    Require(a.far_passes==1 && !a.reused && a.far_bound,"first frame renders the 512 m map");
    Require(b.far_passes==0 && b.reused==1 && b.far_bound,"next frame keeps it and lighting still samples it");
    Require(!memcmp(b.far_view.m,a.far_view.m,64),"the kept map is read with the matrix it was rendered with");
    Require(b.record_completed && !memcmp(b.record.m,a.far_view.m,64),"the trace sees the kept render's record");
    Require(c.far_passes==1 && !c.reused,"interval 2 renders the third frame again");
    // A camera jump past the hole slack, and a lightning turn, render at once.
    input.position[0]+=2;const Frame d=Render(game,input,13);Require(d.far_passes==1 && memcmp(d.far_view.m,c.far_view.m,64),"moved camera renders now");
    input.position[0]-=2;Render(game,input,14);
    input.lighting.sun_direction[0]=-.4f;const Frame e=Render(game,input,15);Require(e.far_passes==1 && !e.reused,"turned sun renders now");
    // r4_sun_far_interval 1 renders every frame.
    input.lighting.sun_far_interval=1;const Frame g1=Render(game,input,16),g2=Render(game,input,17);
    Require(g1.far_passes==1 && g2.far_passes==1 && !g2.reused,"interval 1 is the old behaviour");
    // A snapshot frame renders every pass.
    input.lighting.sun_far_interval=2;Render(game,input,18);g_render_snapshot_active=true;const Frame s=Render(game,input,19);g_render_snapshot_active=false;
    Require(s.far_passes==1,"snapshot frames render the 512 m map");
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty() && !f->leases,"no resource kept past the session");
}
}
// P0-S11 r4_sun_shadow_faces: the cull rule, then the faces each real sun
// pass stores (recorded per draw with the depth target it writes).
std::vector<std::pair<KichR4Handle,uint32_t> > shadow_culls;
int32_t ObserveCull(KichR4Handle device,const KichR4DeferredDrawDesc* d){const int32_t result=DDraw(device,d);
    if(!result && !active_pass.color_count && active_pass.depth)shadow_culls.push_back(std::make_pair(active_pass.depth,d->state.draw.cull_mode));return result;}
void CasterFaces(){
    const uint32_t back=KICH_R4_CULL_BACK,front=KICH_R4_CULL_FRONT,none=KICH_R4_CULL_NONE;
    Require(KichR4SunCasterCull(back,0,0,false)==back && KichR4SunCasterCull(back,0,0,true)==back,"faces 0: front faces everywhere");
    Require(KichR4SunCasterCull(back,0,1,false)==front && KichR4SunCasterCull(back,0,1,true)==back,"faces 1: R2 far back faces, contact front faces");
    Require(KichR4SunCasterCull(back,0,2,true)==front && KichR4SunCasterCull(none,0,2,false)==none && KichR4SunCasterCull(none,0,1,false)==none,"two-sided casters stay two-sided");
    Require(KichR4SunCasterCull(back,1,1,false)==none && KichR4SunCasterCull(front,2,1,false)==back,"r2_sun_shadow_cull 1/2 keep their meaning");
    Require(KichR4SunContactPhase("sun_2") && !KichR4SunContactPhase("sun_0") && !KichR4SunContactPhase("sun_1") && !KichR4SunContactPhase("local_0_face_2") && !KichR4SunContactPhase(NULL),"contact phase");
    for(uint32_t faces:{0u,1u,2u}){Fixture fixture;f=&fixture;ResetDeferredRecorder();shadow_culls.clear();
        auto apis=DeferredAPIs();apis.deferred.draw=ObserveCull;const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);
        game.deferred.native_mode=true;KichR4GameFrame input=DeferredInput();input.engine_frame=40;input.lighting.sun_shadow_faces=faces;
        input.lighting.sun_direction[0]=.3f;input.lighting.sun_direction[1]=-.9f;input.lighting.sun_direction[2]=.2f;
        Require(game.Render(input)==1,"faces frame submitted");
        unsigned sun_front=0,sun_back=0,sun_draws=0;
        for(const auto& d:shadow_culls)if(d.first==game.deferred.sun_map || d.first==game.deferred.sun_far_map){++sun_draws;sun_front+=d.second==front;sun_back+=d.second==back;}
        Require(sun_draws>0,"sun casters drawn");
        if(faces==0)Require(sun_front==0 && sun_back>0,"faces 0 keeps the material's back-face cull in the 128/512 m maps");
        else Require(sun_back==0 && sun_front>0,"faces 1/2 store back faces only in the 128/512 m maps (R2 sun far)");
        KichR4GeometryReport report={};Require(game.Stop(report)==0,"faces session stops");}
}
int main(){try{Rule();SubmitStages();Planner();CasterFaces();
    std::printf("P0-S11 sun far reuse: %d checks passed (rule, submit profile, real planner)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
