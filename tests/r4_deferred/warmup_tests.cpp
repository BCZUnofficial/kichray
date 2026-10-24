// Exercise the actual deferred world -> loading/UI composition in one frame.
// Recording validates API ordering/ownership; it does not simulate GPU pixels.
#include "recording.h"

namespace {
struct WarmupDraw {std::string pixel;KichR4PassDesc pass;KichR4DeferredDrawDesc draw;};
std::vector<WarmupDraw> warmup_draws;
std::map<KichR4Handle,unsigned> buffer_updates;
int32_t WarmupUpdate(KichR4Handle device,KichR4Handle handle,const void* data,uint32_t bytes){
    const int32_t hr=DUpdate(device,handle,data,bytes);if(!hr)++buffer_updates[handle];return hr;
}
int32_t WarmupRecord(KichR4Handle device,const KichR4DeferredDrawDesc* draw){
    const int32_t hr=DDraw(device,draw); // Also checks every SRV against all active attachments.
    if(!hr)warmup_draws.push_back({draw_entries.back(),active_pass,*draw});
    return hr;
}
kich_r4_capture::Batch WarmupBatch(){
    using namespace kich_r4_capture;Batch batch;batch.Begin(960,540,17);batch.renderer_seen=batch.ui_seen=true;
    for(unsigned triangle=0;triangle<5;++triangle){const uint32_t first=static_cast<uint32_t>(batch.indices.size());
        for(unsigned i=0;i<3;++i){Vertex v={};v.p[0]=i==0?-.1f:i==1?0:.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=.5f;v.p[3]=1;
            v.color=0xffffffff;v.normal[1]=v.normal[3]=1;v.world[0]=v.p[0];v.world[1]=v.p[1];v.world[2]=2;
            batch.indices.push_back(static_cast<uint32_t>(batch.vertices.size()));batch.vertices.push_back(v);}
        Style style;if(triangle){style.blend=1;style.write=0;}if(triangle>=2)style.depth=0;
        Command command(first,3,triangle>=2?2:0,"base",style);
        command.effect=triangle==1?5:triangle==3?7:triangle==4?8:0;
        batch.commands.push_back(command);
    }
    batch.models=1;batch.ui=3;return batch;
}
int WarmupScenario(unsigned fail_at=0,int32_t failure=error){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();warmup_draws.clear();buffer_updates.clear();
    auto apis=DeferredAPIs();apis.geometry.update_buffer=WarmupUpdate;apis.deferred.draw=WarmupRecord;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);
    game.deferred.native_mode=true;auto input=DeferredInput();input.engine_frame=17;input.allowed=false;input.warmup=true;
    input.postprocess[0]=input.postprocess[1]=input.postprocess[2]=1;input.postprocess[3]=.5f;input.postprocess[4]=.7f;
    auto batch=WarmupBatch();
    Require(game.frame.Start(input.window,input.width,input.height)==0 && game.level.Start(game.frame,game.name)==0,"cold native level starts during precache");
    const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};
    Require(game.frame.BeginClear(clear)==0,"warmup begins one presentation frame");
    game.level.texture_registry.Begin();
    Require(game.deferred.Draw(game.frame,game.level,game.stream,input,&batch,0)==0,"real static scene and distortion render under the loading screen");
    unsigned geometry=0,distortion=0;
    for(const auto& draw:warmup_draws){geometry+=draw.pixel=="PSGeometry";distortion+=draw.pixel=="PSDistortionComposite";}
    Require(geometry>0 && distortion==1 && game.deferred.frame_sun_cascades==2,"warmup exercises real geometry, sun maps and distortion ping-pong");
    const KichR4Handle scene=game.deferred.scene_color,final=game.deferred.final_color,vb=game.stream.vb,ib=game.stream.ib;
    Require(scene && final && scene!=final,"post-distortion scene and final targets remain distinct");
    const unsigned vb_updates=buffer_updates[vb],ib_updates=buffer_updates[ib],stream_frames=game.stream.frames;
    const unsigned created=f->created;const size_t first_draw=warmup_draws.size(),first_pass=passes.size();
    const int first_call=f->calls;f->failure=failure;f->fail=fail_at?first_call+int(fail_at):0;
    const int32_t result=game.deferred.DrawMenu(game.frame,game.level,game.stream,input,batch,true);
    const int menu_calls=f->calls-first_call;
    Require(buffer_updates[vb]==vb_updates && buffer_updates[ib]==ib_updates && game.stream.frames==stream_frames,
        "loading composition must reuse the stream already uploaded by world draw");
    if(fail_at){
        Require(!f->failed_name.empty() && result==(failure<0?failure:KICH_R4_E_STATE) && game.frame.failure==result,
            "failed or unexpected positive menu API result propagates without a successful frame");
        Require(game.frame.rendered==0,"failed menu does not present the previously rendered world");
    }else{
        Require(result==0 && f->created==created,"menu composition needs no new resource after world warmup");
        Require(passes[first_pass].colors[0]==scene && passes[first_pass].clear_flags==KICH_R4_CLEAR_COLOR,
            "loading scene is cleared instead of retaining warmed world pixels");
        Require(game.deferred.scene_color==scene && game.deferred.final_color==final,"menu never aliases or swaps the post-distortion target pair");
        Require(game.deferred.native_models.prepared==&batch && game.deferred.native_models.frame==batch.engine_frame,
            "after-scene menu preserves the prepared model frame instead of resetting it");
        unsigned ui=0,lens=0,copy=0,posts=0;
        for(size_t i=first_draw;i<warmup_draws.size();++i){const auto& draw=warmup_draws[i];
            Require(draw.pixel!="PSGeometry" && draw.pixel!="PSParticleDistortionMask" && draw.pixel!="PSDistortionComposite",
                "loading output cannot replay world geometry or scene distortion");
            if(draw.pixel=="PSDistortion"){++lens;Require(draw.pass.colors[0]==final && draw.draw.textures[1]==scene,
                "menu lens samples the newly cleared loading scene into the other target");}
            if(draw.pixel=="PSPlain" && draw.draw.state.draw.vertex_buffer==vb)++ui;
            if(draw.pixel=="PSPlain" && draw.draw.state.draw.vertex_buffer!=vb){++copy;Require(draw.draw.textures[0]==scene && draw.pass.colors[0]==final,
                "menu copy consumes loading scene, not the warmed world result");}
            if(draw.pixel=="PSPostprocess"){++posts;Require(i+1==warmup_draws.size() && draw.pass.color_count==1 && !draw.pass.colors[0] && !draw.pass.depth && draw.draw.textures[0]==final,
                "last output is menu postprocess to swapchain with no stale depth attachment");}
        }
        Require(ui==2 && lens==1 && copy==1 && posts==1,"loading UI, lens, overlay and postprocess are composed once");
        const auto& constants=draw_constants.back();Require(constants.alpha[3]==0 && constants.light_position[0]==0 && constants.light_position[1]==0 && constants.light_position[2]==0 && constants.light_position[3]==0,
            "final loading postprocess suppresses world gray, blur, duality and noise");
        Require(game.frame.Present()==0 && game.frame.rendered==1,"warmup world and loading UI share exactly one Present");
        // First ordinary game frame uses warmed targets, shaders, texture and
        // stream storage; its own distortion may swap the same target pair.
        input.allowed=true;input.warmup=false;++input.engine_frame;batch.engine_frame=input.engine_frame;
        Require(game.frame.BeginClear(clear)==0,"first gameplay frame begins");game.level.texture_registry.Begin();
        Require(game.deferred.Draw(game.frame,game.level,game.stream,input,&batch,0)==0 && game.frame.Present()==0,"first gameplay frame renders after precache");
        Require(f->created==created && game.stream.vb==vb && game.stream.ib==ib && game.deferred.scene_color==final && game.deferred.final_color==scene,
            "first gameplay frame reuses warmed resources including the distortion target pair");
        Require(buffer_updates[vb]==vb_updates+1 && buffer_updates[ib]==ib_updates+1,"next game frame uploads current stream exactly once");
    }
    f->fail=0;KichR4GeometryReport report={};Require(game.Stop(report)==0,"warmup resources clean up after success or failure");
    Require(f->objects.empty() && f->created==f->released && !f->leases,"warmup retains no temporary GPU handles or borrowed readers");
    return menu_calls;
}
}
int main(){try{(void)&Scenario;const int calls=WarmupScenario();
    for(int fail=1;fail<=calls;++fail){WarmupScenario(unsigned(fail));WarmupScenario(unsigned(fail),11);}
    std::printf("E1.6 world/distortion -> loading UI warmup: %d checks, %d menu failure positions passed (recording API, no GPU pixels)\n",checks,calls);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
