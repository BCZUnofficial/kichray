#include "../r4_deferred/recording.h"
#include "../../xr_3da/KichR4Game.cpp"
#include <string>
#include <vector>
namespace {
bool missing_dll=false,bad_api=false,throw_api=false,fail_cleanup=false,missing_stream=false,missing_deferred=false;
int loads=0,unloads=0,api_getters=0;
std::vector<std::string> logs;
void Log(const char* text){logs.push_back(text);}
bool Logged(const char* token){for(const auto& line:logs)if(line.find(token)!=std::string::npos)return true;return false;}
uint32_t Version(){fesetround(FE_UPWARD);return KICH_R4_BRIDGE_ABI_VERSION;}
int32_t GameFrameAPI(uint32_t,KichR4FrameAPI* out,uint32_t){
    ++api_getters;if(throw_api)throw std::runtime_error("injected API exception");
    *out=DeferredAPIs().frame;
    if(bad_api)++out->pointer_size;
    return 0;
}
int32_t FpuBegin(KichR4Handle device){fesetround(FE_DOWNWARD);return Begin(device);}
int32_t RuntimeDestroy(KichR4Handle d,KichR4GeometryReport* report,uint32_t size){if(fail_cleanup)return error;return Destroy(d,report,size);}
int32_t GameResourceAPI(uint32_t,KichR4ResourceAPI* out,uint32_t){
    *out=DeferredAPIs().resources;return 0;
}
int32_t GameGeometryAPI(uint32_t,KichR4GeometryAPI* out,uint32_t){
    *out=DeferredAPIs().geometry;out->destroy_device=RuntimeDestroy;return 0;
}
int32_t GameLevelAPI(uint32_t,KichR4LevelAPI* out,uint32_t){
    KichR4LevelSession level;KichR4FrameSession frame={};Configure(level,frame);*out=level.api;return 0;
}
int32_t RuntimeStreamPipeline(KichR4Handle,const KichR4PipelineDesc*,KichR4Handle*){throw std::runtime_error("unexpected stream pipeline in static-only runtime fixture");}
int32_t RuntimeStreamDraw(KichR4Handle,const KichR4StreamDrawDesc*){throw std::runtime_error("unexpected stream draw in static-only runtime fixture");}
int32_t GameStreamAPI(uint32_t,KichR4StreamAPI* out,uint32_t){
    const KichR4StreamAPI api={sizeof(api),KICH_R4_STREAM_ABI,sizeof(void*),0,RuntimeStreamPipeline,RuntimeStreamDraw};*out=api;return 0;
}
int32_t GameDeferredAPI(uint32_t,KichR4DeferredAPI* out,uint32_t){*out=DeferredAPIs().deferred;return 0;}
KichR4GameFrame RuntimeInput() {
    return DeferredInput();
}
void ResetRuntime(Fixture& fixture) {
    f=&fixture;Require(!state.scene && !state.module,"previous runtime released before reset");state=State();
    ResetDeferredRecorder();logs.clear();missing_dll=bad_api=throw_api=fail_cleanup=missing_stream=missing_deferred=false;loads=unloads=api_getters=0;fesetround(FE_TONEAREST);
}
bool Init(const char* flags="-kich_r4_game") {
    const KichR4LevelFiles fs={Open,Close};return KichR4_GameInitialize(flags,Log,&fs);
}
void PresentFrame(const KichR4GameFrame& input,bool expected) {
    if(KichR4_CaptureState()){KichR4_CaptureBeginFrame(input.width,input.height,input.engine_frame);
        KichR4_CaptureState()->frame.renderer_seen=KichR4_CaptureState()->frame.ui_seen=true;}
    KichR4_GamePrepare(input);Require(KichR4_GamePresentPrepared()==expected,"one presenter chosen for this game frame");
    Require(!KichR4_GamePresentPrepared(),"prepared frame consumed once");
}
void Lifecycle() {
    Fixture fixture;ResetRuntime(fixture);
    Require(!Init("-kich_r4_level l01_escape") && !loads && !KichR4_GameWindowed(),"legacy/viewer mode unchanged without exact switch");
    Require(Init() && loads==1 && api_getters==1 && fegetround()==FE_TONEAREST,"game ABI load restores FPU environment");
    Require(KichR4_GameWindowed() && !f->live,"armed game forces windowed but creates no device before level");
    state.api.frame.begin_frame=FpuBegin;
    KichR4_GameLevelStart("l01_escape");KichR4GameFrame input=RuntimeInput();input.allowed=false;
    PresentFrame(input,false);Require(!state.scene && !f->live,"loading/menu frames stay on DX9");
    input.allowed=true;PresentFrame(input,false);
    Require(!state.scene && Logged("UI handshake"),"mismatched old xrGame cannot hide its UI behind R4 output");
    KichR4_GameSetModal(false);PresentFrame(input,true);
    Require(fegetround()==FE_TONEAREST && Logged("first GAME CAMERA DX11 frame"),"native render restores FPU and logs real camera frame");
    const uint32_t frames=state.scene->frame.rendered;
    KichR4_GameSetModal(true);PresentFrame(input,false);Require(state.scene->frame.rendered==frames,"inventory/dialog UI selects DX9 automatically");
    KichR4_GameSetModal(false);PresentFrame(input,true);
    input.toggle_down=true;PresentFrame(input,false);Require(!state.scene->enabled,"F8 legacy comparison works");
    input.toggle_down=false;PresentFrame(input,false);input.toggle_down=true;PresentFrame(input,true);input.toggle_down=false;
    KichR4_GameBeforeReset();Require(!f->live && !state.scene && state.module && state.level[0],"vid_restart destroys R4 before DX9 Reset and retains level identity");
    PresentFrame(input,true);Require(state.generations==2,"first post-reset game frame recreates its R4 owner");
    KichR4_GameLevelStop();Require(!f->live && !state.level[0] && Logged("cleanup=PASS"),"level unload releases all GPU objects");
    PresentFrame(input,false);
    KichR4_GameLevelStart("l01_escape");KichR4_GameSetModal(false);PresentFrame(input,true);
    KichR4_GameShutdown();Require(!f->live && !state.module && !state.scene && unloads==1,"shutdown unloads DLL after final device");
    Require(fegetround()==FE_TONEAREST,"cleanup restores game FPU");KichR4_GameShutdown();
}
void Failures() {
    for(unsigned which=0;which<5;++which) {
        Fixture fixture;ResetRuntime(fixture);missing_dll=which==0;bad_api=which==1;throw_api=which==2;missing_stream=which==3;missing_deferred=which==4;
        Require(Init() && state.disabled && !state.module && !f->live,"DLL/ABI exceptions leave playable legacy mode");
        KichR4_GameLevelStart("l01_escape");PresentFrame(RuntimeInput(),false);KichR4_GameShutdown();
    }
    {
        Fixture fixture;ResetRuntime(fixture);Init();KichR4_GameLevelStart("l01_escape");
        KichR4_GameSetModal(false);f->fail=4;PresentFrame(RuntimeInput(),false);
        Require(state.disabled && !state.scene && !f->live && Logged("DISABLED: step="),"frame failure cleans partial resources and falls back in same frame");
        KichR4_GameShutdown();
    }
    {
        Fixture fixture;ResetRuntime(fixture);Init();KichR4_GameLevelStart("l01_escape");KichR4_GameSetModal(false);PresentFrame(RuntimeInput(),true);
        fail_cleanup=true;KichR4_GameShutdown();
        Require(state.disabled && state.scene && state.scene->frame.device && state.module && !unloads,"DLL and owner retained after two cleanup failures");
        PresentFrame(RuntimeInput(),false);fail_cleanup=false;KichR4_GameShutdown();
        Require(!state.scene && !state.module && !f->live && unloads==1,"retained owner can finish safe cleanup later");
    }
    {
        Fixture fixture;ResetRuntime(fixture);Init();KichR4_GameLevelStart("l01_escape");KichR4_GameSetModal(false);
        KichR4GameFrame input=RuntimeInput();PresentFrame(input,true);
        input.toggle_down=true;PresentFrame(input,false);input.toggle_down=false;
        f->fail=f->calls+1;KichR4_GameBeforeReset();
        Require(!state.disabled && !state.scene && !f->live && Logged("cleanup retry"),"transient cleanup failure retried before reset");
        PresentFrame(input,false);Require(state.scene && !state.scene->enabled && !f->live,"F8 comparison preference survives vid_restart");
        input.toggle_down=true;PresentFrame(input,true);KichR4_GameShutdown();
    }
}
void CaptureGating() {
    Fixture fixture;ResetRuntime(fixture);Init();KichR4_GameLevelStart("l01_escape");KichR4_GameSetModal(false);
    const KichR4GameFrame input=RuntimeInput();
    for(unsigned which=0;which<5;++which){KichR4_CaptureBeginFrame(input.width,input.height,input.engine_frame);
        kich_r4_capture::Batch& batch=KichR4_CaptureState()->frame;batch.renderer_seen=batch.ui_seen=true;
        switch(which){case 0:batch.renderer_seen=false;break;case 1:batch.ui_seen=false;break;case 2:++batch.engine_frame;break;
            case 3:batch.ui_scope=1;break;case 4:batch.Fail("unsupported UI input");break;}
        KichR4_GamePrepare(input);Require(!KichR4_GamePresentPrepared() && !state.scene && !state.disabled,"incomplete capture keeps the entire legacy frame without creating a partial native scene");}
    Require(Logged("renderer capture handshake") && Logged("HUD capture handshake") && Logged("unsupported UI input"),"missing matched binaries and unsupported capture have actionable diagnostics");
    const size_t before=logs.size();const std::string long_reason="model="+std::string(220,'a')+"; invalid active 2W weight: vertex=17 bones=0/1 bits=0x7FC12345";
    for(unsigned i=0;i<4;++i){KichR4_CaptureBeginFrame(input.width,input.height,input.engine_frame);KichR4_CaptureState()->frame.Fail(long_reason.c_str());
        KichR4_GamePrepare(input);Require(!KichR4_GamePresentPrepared(),"long model diagnostic selects legacy frame");}
    unsigned repeated=0;for(size_t i=before;i<logs.size();++i)if(logs[i].find("legacy frame fallback")!=std::string::npos)++repeated;
    Require(repeated==1 && Logged("vertex=17 bones=0/1"),"long asset diagnostics retain details without logging every frame");
    const size_t after=logs.size();KichR4_CaptureBeginFrame(input.width,input.height,input.engine_frame);
    KichR4_CaptureState()->frame.Fail((long_reason+"; another cause").c_str());KichR4_GamePrepare(input);
    Require(logs.size()==after+1 && Logged("another cause"),"different reasons beyond the same long asset prefix are logged separately");
    PresentFrame(input,true);Require(state.scene && !state.disabled,"complete capture resumes native rendering after temporary fallback");KichR4_GameShutdown();
}
void UnlitComparison(){
    Fixture fixture;ResetRuntime(fixture);Require(Init("-kich_r4_game -kich_r4_unlit"),"exact optional unlit flag accepted");
    KichR4_GameLevelStart("l01_escape");KichR4_GameSetModal(false);auto input=RuntimeInput();input.debug_down=true;PresentFrame(input,true);
    Require(state.scene && !state.scene->use_deferred && !state.scene->deferred.frames && !state.scene->debug && targets.empty(),"unlit comparison uses prior native DX11 path without G-buffer or F9");
    KichR4_GameShutdown();Require(!f->live && !state.module,"unlit ownership cleaned");
    Require(!kich_r4_game::Requested("-kich_r4_unlit_extra","-kich_r4_unlit") && kich_r4_game::Requested("\"-kich_r4_unlit\"","-kich_r4_unlit"),"unlit flag uses whole-token parser");
}
}
DWORD GetModuleFileNameW(HMODULE,wchar_t* path,DWORD capacity){const wchar_t text[]=L"C:\\game\\bin\\XR_3DA.exe";Require(capacity>=sizeof(text)/sizeof(text[0]),"path capacity");memcpy(path,text,sizeof(text));return sizeof(text)/sizeof(text[0])-1;}
HMODULE LoadLibraryW(const wchar_t* path){++loads;Require(!wcscmp(path,L"C:\\game\\bin\\xrRender_R4.dll"),"R4 loaded only from executable directory");return missing_dll?NULL:reinterpret_cast<HMODULE>(1);}
BOOL FreeLibrary(HMODULE){Require(!f->live,"DLL never unloaded while its device is live");++unloads;return 1;}
DWORD GetLastError(){return 126;}
FARPROC GetProcAddress(HMODULE,const char* name){
    if(!strcmp(name,KICH_R4_EXPORT_VERSION))return reinterpret_cast<FARPROC>(Version);
    if(!strcmp(name,KICH_R4_EXPORT_FRAME))return reinterpret_cast<FARPROC>(GameFrameAPI);
    if(!strcmp(name,KICH_R4_EXPORT_RESOURCES))return reinterpret_cast<FARPROC>(GameResourceAPI);
    if(!strcmp(name,KICH_R4_EXPORT_GEOMETRY))return reinterpret_cast<FARPROC>(GameGeometryAPI);
    if(!strcmp(name,KICH_R4_EXPORT_LEVEL))return reinterpret_cast<FARPROC>(GameLevelAPI);
    if(!strcmp(name,KICH_R4_EXPORT_STREAM))return missing_stream?NULL:reinterpret_cast<FARPROC>(GameStreamAPI);
    if(!strcmp(name,KICH_R4_EXPORT_DEFERRED))return missing_deferred?NULL:reinterpret_cast<FARPROC>(GameDeferredAPI);
    return NULL;
}
BOOL IsWindow(HWND window){return window!=NULL;}
int GetWindowTextW(HWND,wchar_t* out,int capacity){const wchar_t title[]=L"STALKER";Require(capacity>=8,"title capacity");memcpy(out,title,sizeof(title));return 7;}
BOOL SetWindowTextW(HWND,const wchar_t*){return 1;}
int main(){try{(void)&Scenario;Lifecycle();Failures();CaptureGating();UnlitComparison();printf("B4.4 production DLL loader / game runtime: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
