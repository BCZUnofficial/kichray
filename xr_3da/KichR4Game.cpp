/* No engine PCH or D3D9 headers: this unit owns the optional R4 game bridge. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include "KichR4GameSession.h"
#include "KichR4Capture.h"

namespace {
struct State {
    bool requested,disabled,modal,pending,dx11,title_saved,preferred_dx11,ui_seen,ui_wait_logged,unlit;
    HMODULE module;
    kich_r4_game::APIs api;
    KichR4LevelFiles files;
    KichR4LogFn log;
    kich_r4_game::Session* scene;
    KichR4GameFrame input;
    char level[64];
    HWND window;
    wchar_t title[256];
    uint32_t generations,cleanups;
    uint32_t capture_fallbacks;
    uint64_t capture_reason;
};
State state={};
uint64_t ReasonKey(const char* reason) {
    uint64_t key=14695981039346656037ull;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(reason);*p;++p){key^=*p;key*=1099511628211ull;}
    return key;
}
struct FpuEnvironment {
    fenv_t environment;bool saved;
    FpuEnvironment():saved(fegetenv(&environment)==0){}
    ~FpuEnvironment(){if(saved)fesetenv(&environment);}
};
void Write(const char* format,...) {
    if(!state.log)return;
    char message[1024];va_list args;va_start(args,format);vsnprintf(message,sizeof(message),format,args);va_end(args);
    message[sizeof(message)-1]=0;state.log(message);
}
void Mode(bool dx11) {
    if(dx11==state.dx11)return;
    state.dx11=dx11;
    Write("[R4 bridge B4.3b] output=%s; same game HWND; F8 toggles output",dx11?"DX11 world + models + hands + HUD":"DX9 legacy / menus");
    if(state.window && IsWindow(state.window)) {
        if(dx11)SetWindowTextW(state.window,L"Kich-Ray DX11 B4.4 | G-buffer + lighting | F8 legacy | F9 buffers");
        else if(state.title_saved)SetWindowTextW(state.window,state.title);
    }
}
#if defined(_MSC_VER)
int32_t ExceptionResult(uint32_t code) {return static_cast<int32_t>(code)<0?static_cast<int32_t>(code):KICH_R4_E_STATE;}
#endif
bool ReadAPIs() {
    const KichR4GetBridgeVersionFn version=reinterpret_cast<KichR4GetBridgeVersionFn>(GetProcAddress(state.module,KICH_R4_EXPORT_VERSION));
    const KichR4GetFrameAPIFn frame=reinterpret_cast<KichR4GetFrameAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_FRAME));
    const KichR4GetResourceAPIFn resources=reinterpret_cast<KichR4GetResourceAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_RESOURCES));
    const KichR4GetGeometryAPIFn geometry=reinterpret_cast<KichR4GetGeometryAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_GEOMETRY));
    const KichR4GetLevelAPIFn level=reinterpret_cast<KichR4GetLevelAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_LEVEL));
    const KichR4GetStreamAPIFn stream=reinterpret_cast<KichR4GetStreamAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_STREAM));
    const KichR4GetDeferredAPIFn deferred=reinterpret_cast<KichR4GetDeferredAPIFn>(GetProcAddress(state.module,KICH_R4_EXPORT_DEFERRED));
    return version && frame && resources && geometry && level && stream && deferred && version()==KICH_R4_BRIDGE_ABI_VERSION &&
        frame(KICH_R4_FRAME_ABI,&state.api.frame,sizeof(state.api.frame))==0 &&
        resources(KICH_R4_RESOURCE_ABI,&state.api.resources,sizeof(state.api.resources))==0 &&
        geometry(KICH_R4_GEOMETRY_ABI,&state.api.geometry,sizeof(state.api.geometry))==0 &&
        level(KICH_R4_LEVEL_ABI,&state.api.level,sizeof(state.api.level))==0 &&
        stream(KICH_R4_STREAM_ABI,&state.api.stream,sizeof(state.api.stream))==0 &&
        deferred(KICH_R4_DEFERRED_ABI,&state.api.deferred,sizeof(state.api.deferred))==0 &&
        state.api.Valid() && KichR4StreamSession::ValidAPI(state.api.stream) && KichR4DeferredSession::ValidAPI(state.api.deferred);
}
bool Load() {
    wchar_t path[MAX_PATH];const DWORD length=GetModuleFileNameW(NULL,path,MAX_PATH);
    if(!length || length>=MAX_PATH)return false;
    wchar_t* filename=wcsrchr(path,L'\\');if(!filename)return false;
    ++filename;const wchar_t dll[]=L"xrRender_R4.dll";
    if(static_cast<size_t>(filename-path)+sizeof(dll)/sizeof(dll[0])>MAX_PATH)return false;
    memcpy(filename,dll,sizeof(dll));state.module=LoadLibraryW(path);
    if(!state.module){Write("[R4 bridge B4.3a] DLL unavailable: Win32=%lu",GetLastError());return false;}
    return ReadAPIs();
}
bool GuardLoad() {
#if defined(_MSC_VER)
    __try {return Load();}
    __except(EXCEPTION_EXECUTE_HANDLER) {Write("[R4 bridge B4.3a] DLL/API exception=0x%08lX",GetExceptionCode());return false;}
#else
    try{return Load();}catch(...){return false;}
#endif
}
int32_t StopCpp(kich_r4_game::Session* scene,KichR4GeometryReport* report) {
    try{return scene->Stop(*report);}catch(...){return scene->frame.Record("B4.3a cleanup exception",KICH_R4_E_STATE);}
}
int32_t GuardStop(kich_r4_game::Session* scene,KichR4GeometryReport* report) {
#if defined(_MSC_VER)
    __try {return StopCpp(scene,report);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return scene->frame.Record("B4.3a cleanup SEH",ExceptionResult(GetExceptionCode()));}
#else
    return StopCpp(scene,report);
#endif
}
bool StopScene(const char* reason) {
    state.pending=false;Mode(false);
    if(!state.scene)return true;
    kich_r4_game::Session& scene=*state.scene;
    const bool had_device=scene.frame.device!=0;KichR4GeometryReport report={};
    int32_t hr=GuardStop(&scene,&report);
    if(hr!=0 && scene.frame.device) {
        Write("[R4 bridge B4.3a] cleanup retry: HRESULT=0x%08X",static_cast<uint32_t>(hr));
        hr=GuardStop(&scene,&report);
    }
    const bool balanced=hr==0 && (!had_device || kich_r4_game::Session::Balanced(report));
    Write("[R4 bridge B4.3a] end: reason=%s level=%s; DX11_frames=%u legacy_frames=%llu; submitted_draws=%llu submitted_triangles=%llu; DDS=%u fallback=%u",
        reason,scene.name,scene.frame.rendered,static_cast<unsigned long long>(scene.legacy_frames),
        static_cast<unsigned long long>(scene.level.submitted_draws),static_cast<unsigned long long>(scene.level.submitted_triangles),scene.level.textures_loaded,scene.level.texture_fallbacks);
    if(had_device)Write("[R4 bridge B4.3b] stream submitted: upload_frames=%u models=%llu hands_weapon=%llu HUD=%llu",
        scene.stream.frames,static_cast<unsigned long long>(scene.stream.layer_draws[0]),static_cast<unsigned long long>(scene.stream.layer_draws[1]),static_cast<unsigned long long>(scene.stream.layer_draws[2]));
    if(had_device)Write("[R4 bridge B4.4] deferred: frames=%u shadow_passes=%u local_light_passes=%u debug=%u unlit=%u",
        scene.deferred.frames,scene.deferred.shadow_passes,scene.deferred.local_passes,scene.debug,scene.use_deferred?0u:1u);
    if(had_device)Write("[R4 bridge B4.3a] cleanup=%s: views=%u/%u flags=0x%X pipelines=%u/%u depth=%u/%u resources=%u/%u live=%u payload=%llu; HRESULT=0x%08X",
        balanced?"PASS":"FAIL",report.frame.views_created,report.frame.views_released,report.frame.flags,
        report.geometry.pipelines_created,report.geometry.pipelines_released,report.geometry.depth_created,report.geometry.depth_released,
        report.resources.created,report.resources.released,report.resources.live_resources,static_cast<unsigned long long>(report.resources.payload_bytes),static_cast<uint32_t>(hr));
    if(!balanced)state.disabled=true;
    if(scene.frame.device) {
        state.disabled=true;Write("[R4 bridge B4.3a] live device retained with its DLL; legacy output remains active");return false;
    }
    if(had_device && balanced)++state.cleanups;
    delete state.scene;state.scene=NULL;return true;
}
int32_t RenderCpp(kich_r4_game::Session* scene,const KichR4GameFrame* input) {
    try{return scene->Render(*input,KichR4_CapturedFrame());}
    catch(const std::bad_alloc&){return scene->frame.Record("B4.3a allocation",KICH_R4_E_MEMORY);}
    catch(...){return scene->frame.Record("B4.3a render exception",KICH_R4_E_STATE);}
}
int32_t GuardRender(kich_r4_game::Session* scene,const KichR4GameFrame* input) {
#if defined(_MSC_VER)
    __try {return RenderCpp(scene,input);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return scene->frame.Record("B4.3a frame SEH",ExceptionResult(GetExceptionCode()));}
#else
    return RenderCpp(scene,input);
#endif
}
void UnloadIfIdle() {
    if(state.module && (!state.scene || !state.scene->frame.device)) {
        if(FreeLibrary(state.module))state.module=NULL;
    }
}
}
bool KichR4_GameInitialize(const char* params,KichR4LogFn log,const KichR4LevelFiles* files) {
#if defined(DEDICATED_SERVER) || defined(_EDITOR)
    (void)params;(void)log;(void)files;return false;
#else
    if(state.requested)return true;
    if(!kich_r4_game::Requested(params))return false;
    FpuEnvironment preserve;state.requested=true;state.preferred_dx11=true;state.log=log;
    state.unlit=kich_r4_game::Requested(params,"-kich_r4_unlit");
    KichR4_CaptureEnable(true,state.log);
    if(files)state.files=*files;
    if(!state.files.open || !state.files.close || !GuardLoad()) {
        state.disabled=true;KichR4_CaptureEnable(false);UnloadIfIdle();Write("[R4 bridge B4.3b] DISABLED: bridge/files/ABI unavailable; continuing legacy game");return true;
    }
    Write("[R4 bridge B4.4] armed: G-buffer / weather sun + ambient / up to 8 point-spot lights / basic shadow maps / hands + HUD; F8 legacy, F9 buffers; unlit=%u",state.unlit?1u:0u);
    Write("[R4 bridge B4.3b.1] 2W compatibility enabled: equal-bone weights ignored; actual software/hardware skin path; model diagnostics");
    Write("[R4 bridge B4.3b] CPU geometry/pose/UI capture only; no DX9 frame copy or GPU readback; matching EXE, R1/R2, xrGame and R4 required");
    Write("[R4 bridge B4.3a] windowed presentation forced for this process; rs_fullscreen configuration is preserved; standalone viewer flags ignored");
    return true;
#endif
}
bool KichR4_GameWindowed() {return state.requested;}
void KichR4_GameLevelStart(const char* name) {
    if(!state.requested)return;
    FpuEnvironment preserve;
    if(!StopScene("level start"))return;
    state.level[0]=0;state.modal=false;state.ui_seen=state.ui_wait_logged=false;
    if(!kich_r4_game::LevelName(name)) {state.disabled=true;Write("[R4 bridge B4.3a] invalid level name; continuing legacy game");return;}
    memcpy(state.level,name,strlen(name)+1);
    Write("[R4 bridge B4.3a] level attached: %s; waiting for playable camera / end of precache",state.level);
}
void KichR4_GameLevelStop() {
    if(!state.requested)return;
    FpuEnvironment preserve;StopScene("level unload");state.level[0]=0;state.modal=false;state.ui_seen=state.ui_wait_logged=false;
}
void KichR4_GameBeforeReset() {
    if(!state.requested)return;
    FpuEnvironment preserve;StopScene("DX9 device reset");
}
void KichR4_GameSetModal(bool modal) {state.modal=modal;state.ui_seen=true;}
void KichR4_GamePrepare(const KichR4GameFrame& input) {
    if(!state.requested)return;
    if(input.allowed && !state.ui_seen && !state.ui_wait_logged && !state.disabled) {
        Write("[R4 bridge B4.3a] waiting for xrGame UI handshake; keep the matching rebuilt xrGame.dll with this EXE");state.ui_wait_logged=true;
    }
    state.input=input;state.input.allowed=state.input.allowed && !state.modal && state.ui_seen;state.pending=true;
    if(state.input.allowed) {
        const kich_r4_capture::Batch* batch=KichR4_CapturedFrame();
        const bool ready=batch && batch->Ready() && batch->engine_frame==input.engine_frame && batch->width==input.width && batch->height==input.height;
        if(!ready){state.input.allowed=false;++state.capture_fallbacks;
            const char* why=!batch?"CPU capture unavailable":!batch->error.empty()?batch->error.c_str():
                !batch->renderer_seen?"renderer capture handshake missing; rebuild R1/R2":!batch->ui_seen?"HUD capture handshake missing; rebuild xrGame":"incomplete or mismatched CPU frame";
            const uint64_t reason_key=ReasonKey(why);
            if(state.capture_reason!=reason_key || state.capture_fallbacks%300==0){Write("[R4 bridge B4.3b] legacy frame fallback: %s; engine_frame=%u count=%u",why,input.engine_frame,state.capture_fallbacks);
                state.capture_reason=reason_key;}}
        else state.capture_reason=0;
    }
}
bool KichR4_GamePresentPrepared() {
    if(!state.requested || !state.pending)return false;
    FpuEnvironment preserve;state.pending=false;
    if(state.disabled || !state.level[0]) {Mode(false);return false;}
    if(!state.scene) {
        if(!state.input.allowed || !state.input.active)return false;
        try {state.scene=new kich_r4_game::Session(state.api,state.files,state.level,state.log);
            state.scene->enabled=state.preferred_dx11;state.scene->use_deferred=!state.unlit;++state.generations;}
        catch(...) {state.disabled=true;Write("[R4 bridge B4.3a] allocation failed; continuing legacy game");return false;}
        if(!state.title_saved) {
            state.window=static_cast<HWND>(state.input.window);
            GetWindowTextW(state.window,state.title,static_cast<int>(sizeof(state.title)/sizeof(state.title[0])));state.title_saved=true;
        }
    }
    const uint32_t previous=state.scene->frame.rendered;
    const bool enabled=state.scene->enabled;
    const uint32_t previous_debug=state.scene->debug;
    const int32_t result=GuardRender(state.scene,&state.input);
    state.preferred_dx11=state.scene->enabled;
    if(previous_debug!=state.scene->debug)Write("[R4 bridge B4.4] F9: buffer=%u (0 lit, 1 albedo, 2 normal, 3 distance, 4 light, 5 hemi)",state.scene->debug);
    if(enabled!=state.scene->enabled)Write("[R4 bridge B4.3a] F8: requested output=%s",state.scene->enabled?"DX11":"legacy");
    if(result<0) {
        Write("[R4 bridge B4.3a] DISABLED: step=%s HRESULT=0x%08X; detail=%s; shader=%s; continuing legacy game",
            state.scene->frame.step?state.scene->frame.step:"unknown",static_cast<uint32_t>(result),state.scene->level.detail.c_str(),state.scene->level.bridge.shader_error);
        state.disabled=true;StopScene("R4 failure");KichR4_CaptureEnable(false);return false;
    }
    if(!previous && state.scene->frame.rendered)Write("[R4 bridge B4.3a] first GAME CAMERA DX11 frame presented: level=%s HWND=%p viewport=%ux%u; camera=(%.3f,%.3f,%.3f); engine_frame=%u; scene_generation=%u",
        state.level,state.input.window,state.input.width,state.input.height,state.input.position[0],state.input.position[1],state.input.position[2],state.input.engine_frame,state.generations);
    if(state.scene->frame.rendered!=previous && (!previous || state.scene->frame.rendered%300==0)) {
        const kich_r4_capture::Batch* batch=KichR4_CapturedFrame();
        if(batch)Write("[R4 bridge B4.3b] native layers: engine_frame=%u models=%u hands_weapon=%u HUD=%u vertices=%u triangles=%u upload_frames=%u; registry=%u MiB",
            state.input.engine_frame,batch->models,batch->hands,batch->ui,static_cast<uint32_t>(batch->vertices.size()),static_cast<uint32_t>(batch->indices.size()/3),state.scene->stream.frames,
            static_cast<uint32_t>(KichR4_CaptureState()->ModelBytes()/(1024u*1024u)));
        Write("[R4 bridge B4.4] CPU lighting: active_lights=%u selected=%u posed_stride=%u deferred_frames=%u",
            KichR4_CaptureState()?static_cast<uint32_t>(KichR4_CaptureState()->lights.size()):0,state.input.lighting.count,
            static_cast<uint32_t>(sizeof(kich_r4_capture::Vertex)),state.scene->deferred.frames);
    }
    if(state.scene->frame.rendered!=previous && state.scene->frame.rendered%300==0)Write("[R4 bridge B4.3a] game frame: engine_frame=%u DX11_frames=%u camera=(%.3f,%.3f,%.3f) visible=%u culled=%u",
        state.input.engine_frame,state.scene->frame.rendered,state.input.position[0],state.input.position[1],state.input.position[2],state.scene->level.visible,state.scene->level.culled);
    Mode(result==1);return result==1;
}
void KichR4_GameShutdown() {
    if(!state.requested)return;
    if(state.disabled && !state.scene && !state.module)return;
    FpuEnvironment preserve;StopScene("shutdown");state.level[0]=0;state.disabled=true;UnloadIfIdle();
    KichR4_CaptureEnable(false);
    Write("[R4 bridge B4.3a] shutdown: scene_generations=%u cleanups=%u DLL=%s",state.generations,state.cleanups,state.module?"retained":"released");
}
