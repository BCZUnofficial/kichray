#include "../r4_level/recording.h"
#include "../../xr_3da/KichR4GameSession.h"
#include <limits>
namespace {
kich_r4_game::APIs GameAPIs() {
    KichR4LevelSession level;KichR4FrameSession frame={};Configure(level,frame);
    const kich_r4_game::APIs a={frame.api,level.bridge.resources,level.bridge.api,level.api,{}};
    Require(a.Valid(),"complete validated game API tables");return a;
}
KichR4GameFrame Input() {
    fixture::Level source;kich_r4_level::Scene scene;
    kich_r4_level::Parse(kich_r4_level::Span(source.level.data(),static_cast<uint32_t>(source.level.size())),
        kich_r4_level::Span(source.geometry.data(),static_cast<uint32_t>(source.geometry.size())),scene);
    kich_r4_level::Camera camera;camera.Reset(scene.low,scene.high);
    KichR4GameFrame input={};input.window=reinterpret_cast<void*>(1);input.width=960;input.height=540;
    input.active=input.allowed=true;input.engine_frame=45;
    const kich_r4_scene::Matrix vp=camera.ViewProjection(input.width,input.height);
    memcpy(input.view_projection,vp.m,sizeof(vp.m));return input;
}
void StopGame(kich_r4_game::Session& session) {
    const bool had_device=session.frame.device!=0;KichR4GeometryReport report={};
    if(session.Stop(report)<0 && session.frame.device)Require(session.Stop(report)==0,"one cleanup retry succeeds");
    if(had_device)Require(kich_r4_game::Session::Balanced(report),"final native ownership report balanced");
    Require(!session.frame.device && !f->live && f->objects.empty() && !f->leases && f->opens==f->closes,"all game level owners released");
}
int GameScenario(int failure,int32_t native_error=error) {
    Fixture fixture;f=&fixture;f->fail=failure;f->failure=native_error;
    const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session session(GameAPIs(),fs,"l01_escape",NULL);
    KichR4GameFrame input=Input();const int32_t result=session.Render(input);
    if(!failure)Require(result==1 && session.frame.rendered==1 && session.level.visible==4,"game camera draws full fixture scene");
    else if(f->failed_name!="source DDS")Require(result<0,"game caller can fall back on a native frame failure");
    const int calls=f->calls;StopGame(session);return calls;
}
void ControlsAndCamera() {
    Fixture fixture;f=&fixture;const KichR4LevelFiles fs={Open,Close};
    kich_r4_game::Session session(GameAPIs(),fs,"l01_escape",NULL);KichR4GameFrame input=Input();
    input.allowed=false;Require(session.Render(input)==0 && !f->live && !f->calls,"menus and loading do not create GPU resources");
    input.allowed=true;input.active=false;Require(session.Render(input)==0 && !f->calls,"inactive application uses no R4 calls");
    input.active=true;input.toggle_down=true;Require(session.Render(input)==0 && !session.enabled && !f->calls,"first F8 can select legacy before lazy creation");
    Require(session.Render(input)==0 && !session.enabled,"held F8 does not retrigger");
    input.toggle_down=false;session.Render(input);input.toggle_down=true;
    Require(session.Render(input)==1 && session.enabled && session.frame.rendered==1,"F8 enables actual game world");
    input.toggle_down=false;session.level.camera.position={1.e6f,1.e6f,1.e6f};
    // Verify roll/asymmetry survive without a transpose or viewer camera reset.
    kich_r4_scene::Matrix roll=kich_r4_scene::Identity();roll.m[0]=.8f;roll.m[1]=.6f;roll.m[4]=-.6f;roll.m[5]=.8f;
    kich_r4_scene::Matrix vp={};memcpy(vp.m,input.view_projection,sizeof(vp.m));vp=kich_r4_scene::Multiply(vp,roll);
    memcpy(input.view_projection,vp.m,sizeof(vp.m));
    Require(session.Render(input)==1 && session.level.visible==4,"external game matrix controls visibility independent of viewer camera");
    kich_r4_scene::Constants uploaded={};memcpy(&uploaded,f->objects.at(session.level.bridge.cb).bytes.data(),sizeof(uploaded));
    const kich_r4_scene::Matrix expected=kich_r4_scene::Multiply(session.level.scene.draws.back().world,vp);
    Require(!memcmp(uploaded.mvp.m,expected.m,sizeof(expected.m)),"exact engine row-vector matrix with tree world transform uploaded");
    const uint32_t draws=f->draws;
    input.allowed=false;input.toggle_down=true;Require(session.Render(input)==0 && f->draws==draws,"modal UI presents legacy and ignores toggle");
    input.allowed=true;Require(session.Render(input)==1 && session.enabled,"key held across modal transition does not toggle");
    input.toggle_down=false;input.width=1280;input.height=720;
    Require(session.Render(input)==1 && session.width==1280 && session.height==720,"vid_mode viewport reaches target resize");
    StopGame(session);
}
void BadInputsAndReports() {
    const KichR4LevelFiles fs={Open,Close};
    for(unsigned which=0;which<7;++which) {
        Fixture fixture;f=&fixture;kich_r4_game::Session session(GameAPIs(),fs,which==0?"../bad":"l01_escape",NULL);
        KichR4GameFrame input=Input();
        switch(which) {
        case 1:input.view_projection[6]=std::numeric_limits<float>::quiet_NaN();break;
        case 2:input.window=NULL;break;case 3:input.width=0;break;case 4:input.height=16385;break;
        case 5:f->files.erase("$game_levels$/l01_escape\\level.geom");break;
        case 6:Require(session.Render(input)==1,"first HWND accepted");input.window=reinterpret_cast<void*>(2);break;
        }
        Require(session.Render(input)<0,"invalid game input / level rejected with diagnostic");StopGame(session);
    }
    for(unsigned result=0;result<2;++result) {
        Fixture fixture;f=&fixture;kich_r4_game::Session session(GameAPIs(),fs,"l01_escape",NULL);KichR4GameFrame input=Input();
        Require(session.Render(input)==1,"level drawn before resize failure");
        input.width=1280;f->fail=f->calls+1;f->failure=result?1:error;
        Require(session.Render(input)<0,"failed resize and unexpected nonzero resize status trigger legacy fallback");StopGame(session);
    }
    KichR4GeometryReport r={};r.struct_size=sizeof(r);r.frame.struct_size=sizeof(r.frame);
    r.resources.struct_size=sizeof(r.resources);r.geometry.struct_size=sizeof(r.geometry);
    Require(kich_r4_game::Session::Balanced(r),"balanced zero report accepted");
    for(unsigned bad=0;bad<11;++bad) {
        KichR4GeometryReport broken=r;
        switch(bad) {case 0:++broken.struct_size;break;case 1:++broken.frame.struct_size;break;case 2:broken.frame.flags=1;break;
        case 3:++broken.frame.views_created;break;case 4:++broken.resources.live_resources;break;case 5:++broken.resources.payload_bytes;break;
        case 6:++broken.resources.created;break;case 7:++broken.geometry.pipelines_created;break;case 8:++broken.geometry.depth_created;break;
        case 9:++broken.resources.struct_size;break;case 10:++broken.geometry.struct_size;break;}
        Require(!kich_r4_game::Session::Balanced(broken),"false cleanup success rejected");
    }
}
}
int main() {try {
    // Keep the shared fixture's B4.2 Scenario covered by its own test binary.
    (void)&Scenario;
    Require(!kich_r4_game::Requested(NULL) && !kich_r4_game::Requested("-kich_r4_game_more") &&
        !kich_r4_game::Requested("-not-kich_r4_game") && !kich_r4_game::Requested("\"-kich_r4_game"),"game switch exact token boundary");
    Require(kich_r4_game::Requested("game.exe \"-kich_r4_game\"\t-kich_r4_level") && kich_r4_game::Requested("-kich_r4_game"),"quoted and unquoted exact switch");
    const int calls=GameScenario(0);for(int i=1;i<=calls;++i)GameScenario(i);
    for(int i=1;i<=calls;++i)GameScenario(i,7);
    ControlsAndCamera();BadInputsAndReports();
    printf("B4.3a game session: %d checks; %d native render failure positions, also unexpected positive results\n",checks,calls);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
