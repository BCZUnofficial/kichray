#include "../../xr_3da/KichR4Game.h"
#include "../../xr_3da/KichR4Frame.h"
#include <vector>
#include <string>
#include <cstdio>
#include <stdexcept>
namespace {unsigned checks=0;void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}}
struct Frame {int failure=0;uint64_t device=1;};
struct Stream {};
struct Batch {};
struct Level {int start_result=0;unsigned starts=0;int Start(Frame&,const char*){++starts;return start_result;}};
struct Deferred {
    std::vector<std::string> draws;int world_result=0,menu_result=0;unsigned debug=UINT32_MAX;bool after_scene=false;
    int Draw(Frame&,Level&,Stream&,const KichR4GameFrame&,const Batch*,unsigned view){draws.push_back("world");debug=view;return world_result;}
    int DrawMenu(Frame&,Level&,Stream&,const KichR4GameFrame&,const Batch&,bool after=false){draws.push_back("UI");after_scene=after;return menu_result;}
};
struct Context {
    bool loaded=false;std::string name="l01_escape";Frame frame;Level level;Stream stream;Deferred deferred;
    unsigned warmup_frames=0,gpu_quarantine=0;struct {unsigned gaps=0;void Gap(){++gaps;}} metrics;
};
struct Native {unsigned debug=5,successful_presents=42;
    struct {bool set_capture=false;} telemetry;
    struct {struct {void profile_mark(uint64_t,unsigned){}} tools;} api;
} native;
struct {bool complete=false;} snapshot_guard;
bool g_render_snapshot_active=false;unsigned cancelled_timings=0,forgotten_timings=0,present_records=0;
std::string snapshot_end;
unsigned RenderSnapshot_CaptureID(){return 3;}unsigned RenderSnapshot_RenderFrameID(){return 4;}
void RenderSnapshot_GPUTiming(unsigned,unsigned,unsigned,const char* status,const void*,unsigned){Require(std::string(status)=="CANCELLED","precache timing cancellation is explicit");++cancelled_timings;}
void ForgetTiming(unsigned){++forgotten_timings;}
void RenderSnapshot_PresentID(unsigned count,bool presented){Require(count==42 && !presented,"cancelled precache is not a successful Present");++present_records;}
void RenderSnapshot_End(const char* why){snapshot_end=why;}
namespace kich_r4_phases {struct Phases {bool eligible=true;void MarkEligible(unsigned,bool value){eligible=value;}} phases;Phases& Global(){return phases;}}
#include "warmup_under_test.h"
int main(){try{
    Batch batch;KichR4GameFrame input={};input.warmup=true;
    Context warmup;Require(StartScene(warmup,input) && warmup.loaded && warmup.level.starts==1,"first native scene loads during precache");
    Require(Dispatch(warmup,input,&batch,0,false)==0 && warmup.deferred.draws==std::vector<std::string>({"world","UI"}),"precache exercises real scene then restores loading UI last");
    Require(warmup.deferred.debug==0 && warmup.deferred.after_scene && warmup.warmup_frames==1,"warmup uses normal shading and reuses scene-uploaded streams for UI");
    Require(!Eligible(warmup,input,0,false,false),"precache never becomes gameplay FPS sample");
    Require(StartScene(warmup,input) && warmup.level.starts==1,"loaded scene reused for subsequent precache frames");
    Context menu;input.warmup=false;Require(StartScene(menu,input) && !menu.loaded && !menu.level.starts,"ordinary menu cannot trigger world load");
    Require(Dispatch(menu,input,&batch,0,false)==0 && menu.deferred.draws==std::vector<std::string>({"UI"}) && !menu.deferred.after_scene,"ordinary menu owns its original stream upload");
    Context unnamed;unnamed.name.clear();input.warmup=true;Require(StartScene(unnamed,input) && !unnamed.loaded,"warmup without named level cannot load a fabricated scene");
    Context failed;failed.level.start_result=-7;Require(!StartScene(failed,input) && !failed.loaded,"scene-load error remains a failure");
    for(unsigned stage=0;stage<3;++stage){Context c;c.loaded=true;if(stage==0)c.deferred.world_result=-8;else if(stage==1)c.deferred.menu_result=-9;else c.frame.failure=-10;
        const int result=Dispatch(c,input,&batch,0,false);Require(result==-(8+int(stage)),"scene/UI/sticky frame error is propagated");
        Require(c.deferred.draws.size()==(stage==0?1u:2u),"failed world does not proceed into UI composition");
        if(stage<2)Require(!c.warmup_frames,"failed scene or UI does not count as completed warmup");}
    Context clear;clear.loaded=true;Require(Dispatch(clear,input,&batch,-11,false)==-11 && clear.deferred.draws.empty(),"clear failure performs no scene or UI work");
    Context game;input.warmup=false;input.allowed=true;Require(StartScene(game,input) && game.loaded,"normal gameplay still loads scene");
    Require(Dispatch(game,input,&batch,0,false)==0 && game.deferred.draws==std::vector<std::string>({"world"}) && game.deferred.debug==5,"normal gameplay keeps diagnostic selection and avoids extra UI redraw");
    native.debug=0;Require(Eligible(game,input,0,false,false),"ordinary gameplay remains eligible");
    Require(!Eligible(game,input,0,true,false) && !Eligible(game,input,0,false,true),"trace and screenshot exclusions preserved");
    input.allowed=false;input.warmup=true;Context cancelled;g_render_snapshot_active=true;
    Require(PreflightResult(cancelled,input,KICH_R4_FRAME_SUSPENDED),"cancelled precache returns success to engine rather than fatal assertion");
    Require(cancelled.metrics.gaps==1 && cancelled.gpu_quarantine==4 && !kich_r4_phases::Global().eligible,"cancelled precache invalidates timing eligibility");
    Require(snapshot_guard.complete && snapshot_end=="PRECACHE_CANCELLED" && cancelled_timings==1 && forgotten_timings==1 && present_records==1,"cancelled snapshot/timing bookkeeping completes exactly once");
    Require(!PreflightResult(cancelled,input,-12) && PreflightResult(cancelled,input,KICH_R4_OK),"actual preflight failures and success keep separate results");
    std::printf("E1.6 native warmup dispatch: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
