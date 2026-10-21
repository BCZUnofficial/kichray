#include "recording.h"
namespace {
kich_r4_capture::Batch Batch(){using namespace kich_r4_capture;kich_r4_capture::Batch batch;batch.Begin(960,540,0);batch.renderer_seen=batch.ui_seen=true;
    for(unsigned i=0;i<3;++i){Vertex v={};v.p[0]=i==0?-.1f:i==1?0:.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=.5f;v.p[3]=1;
        v.color=0xffffffff;v.normal[1]=v.normal[3]=1;v.world[0]=v.p[0];v.world[1]=v.p[1];v.world[2]=2;batch.vertices.push_back(v);batch.indices.push_back(i);}
    Style style;batch.commands.push_back(Command{0,3,0,"base",style});style.max_z=.02f;batch.commands.push_back(Command{0,3,1,"base",style});
    style.max_z=1;style.blend=1;style.write=0;batch.commands.push_back(Command{0,3,0,"base",style});style.depth=0;style.clamp=1;batch.commands.push_back(Command{0,3,2,"base",style});
    batch.models=2;batch.hands=batch.ui=1;return batch;}
int ScenarioDeferred(int failure=0,int32_t error_code=error){Fixture fixture;f=&fixture;ResetDeferredRecorder();f->fail=failure;f->failure=error_code;
    const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);
    KichR4GameFrame input=DeferredInput();input.lighting.count=2;
    KichR4LocalLight& point=input.lighting.lights[0];point.type=1;point.range=15;point.position[1]=2;point.color[0]=1;
    KichR4LocalLight& spot=input.lighting.lights[1];spot.type=2;spot.range=20;spot.direction[2]=1;spot.cone=1;spot.color[1]=1;
    const auto batch=Batch();const int32_t result=game.Render(input,&batch);const int positions=f->calls;
    if(!failure){Require(result==1 && game.deferred.frames==1 && game.frame.rendered==1,"complete deferred frame presented once");
        Require(game.deferred.shadow_passes==8 && game.deferred.local_passes==2,"sun + point six faces + spot shadows and two light passes");
        Require(passes.size()==12 && passes[8].color_count==2 && passes[8].depth==game.deferred.depth,"shadow -> MRT -> irradiance -> combine -> forward/UI phases");
        Require(draw_entries.back()=="PSPlain" && game.stream.layer_draws[0]==2 && game.stream.layer_draws[1]==1 && game.stream.layer_draws[2]==1,"HUD last and main counters exclude repeated shadow/light draws");
        const uint32_t created=f->created;const KichR4Handle vb=game.stream.vb,gbuffer=game.deferred.albedo;
        Require(game.Render(input,&batch)==1 && created==f->created && vb==game.stream.vb,"warm frame reuses native targets/shadows/upload buffers");
        input.debug_down=true;Require(game.Render(input,&batch)==1 && game.debug==1,"F9 selects albedo");
        const size_t draw_count=draw_entries.size();Require(game.Render(input,&batch)==1 && game.debug==1,"held F9 does not cycle repeatedly");
        Require(draw_count<draw_entries.size(),"debug frame still submitted");
        input.debug_down=false;input.toggle_down=true;const auto frames=game.frame.rendered;
        Require(game.Render(input,&batch)==0 && game.frame.rendered==frames,"F8 retains legacy presenter without native submission");
        input.toggle_down=false;game.Render(input,&batch);input.toggle_down=true;Require(game.Render(input,&batch)==1,"F8 returns to R4");
        input.toggle_down=false;input.width=1280;input.height=720;
        Require(game.Render(input,&batch)==1 && game.deferred.albedo!=gbuffer && !f->objects.count(gbuffer),"resize replaces all screen targets and releases old resources");
        const uint32_t shadow_passes=game.deferred.shadow_passes;memset(input.lighting.sun_color,0,12);input.lighting.count=0;
        Require(game.Render(input,&batch)==1 && game.deferred.shadow_passes==shadow_passes,"night without lights skips shadow work");
    }else if(f->failed_name!="source DDS")Require(result<0,"every failed or unexpected-positive native operation propagates to legacy fallback");
    KichR4GeometryReport report={};if(game.Stop(report)<0 && game.frame.device)Require(game.Stop(report)==0,"cleanup retried");
    Require(!game.frame.device && !f->live && f->objects.empty() && !f->leases && f->created==f->released,"partial construction always cleaned without leaked GPU handles/readers");
    return positions;
}
void Invalid(){
    for(unsigned which=0;which<3;++which){Fixture fixture;f=&fixture;ResetDeferredRecorder();const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);
        auto input=DeferredInput();if(which==0)input.lighting.valid=0;else if(which==1)input.lighting.count=9;else memset(input.view_projection,0,64);
        Require(game.Render(input)<0 && !game.frame.rendered,"invalid deferred inputs never present partial image");KichR4GeometryReport r={};game.Stop(r);Require(!f->live && f->objects.empty(),"invalid input cleanup balanced");}
}
}
int main(){try{(void)&Scenario;const int positions=ScenarioDeferred();for(int i=1;i<=positions;++i){ScenarioDeferred(i);ScenarioDeferred(i,7);}Invalid();
    printf("B4.4 pass planner / lighting / reuse / F8-F9 / failures: %d checks, %d failure positions passed\n",checks,positions);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
