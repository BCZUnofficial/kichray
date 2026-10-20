#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "KichR4FrameProbe.h"
#include "KichR4FrameSession.h"
#include "KichR4GeometrySession.h"
#include "KichR4LevelSession.h"

namespace {
const wchar_t class_name[]=L"KichRayDX11_B3_Frame_v1";
const wchar_t window_title[]=L"Kich-Ray DX11 B3 | Resize / Alt+Tab | F11 fullscreen | Space color | Esc -> game";
const wchar_t geometry_title[]=L"Kich-Ray DX11 B4.1 | 1 triangle / 2 cubes | D depth / C cull / P pause | Arrows camera / +/- zoom | F11 | Esc game";
const wchar_t level_title[]=L"Kich-Ray DX11 B4.2 | Loading level... | Esc cancels";
struct WindowState {
    HWND window;
    HINSTANCE instance;
    bool registered,close,active,resize,toggle,borderless;
    uint32_t width,height,palette,activations,mode_changes;
    WINDOWPLACEMENT placement;
    LONG_PTR style;
    bool quit;
    int quit_code;
    KichR4FrameSession session;
    bool geometry_mode;
    KichR4GeometrySession geometry;
    kich_r4_scene::Controls controls;
    uint32_t controls_revision;
    const char* level_name;
    KichR4LevelSession* level;
};
const char* Stage(const WindowState& s){return s.level_name?"B4.2":s.geometry_mode?"B4.1":"B3";}
bool SceneKey(WindowState& s,WPARAM key,bool repeat)
{
    if(s.level){if(!repeat)s.level->Key(static_cast<unsigned>(key));return key=='T' || key=='C' || key=='Z' || key==VK_HOME;}
    if(!s.geometry_mode)return false;
    kich_r4_scene::Action action=kich_r4_scene::Triangle;
    switch(key){
    case '1':action=kich_r4_scene::Triangle;break;case '2':action=kich_r4_scene::Cubes;break;
    case 'D':action=kich_r4_scene::Depth;break;case 'C':action=kich_r4_scene::Cull;break;case 'P':action=kich_r4_scene::Pause;break;
    case VK_HOME:action=kich_r4_scene::CameraReset;break;
    case VK_LEFT:action=kich_r4_scene::Left;repeat=false;break;case VK_RIGHT:action=kich_r4_scene::Right;repeat=false;break;
    case VK_UP:action=kich_r4_scene::Up;repeat=false;break;case VK_DOWN:action=kich_r4_scene::Down;repeat=false;break;
    case VK_ADD:case VK_OEM_PLUS:action=kich_r4_scene::Closer;repeat=false;break;
    case VK_SUBTRACT:case VK_OEM_MINUS:action=kich_r4_scene::Farther;repeat=false;break;
    default:return false;
    }
    if(!repeat){kich_r4_scene::Apply(s.controls,action);++s.controls_revision;}return true;
}
void Write(KichR4LogFn log,const char* format,...)
{
    char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    text[sizeof(text)-1]=0;log(text);
}
void Size(WindowState& state)
{
    RECT r={};
    if(IsIconic(state.window)){state.width=state.height=0;}
    else if(GetClientRect(state.window,&r)) {
        state.width=static_cast<uint32_t>(r.right-r.left);state.height=static_cast<uint32_t>(r.bottom-r.top);
    }
    state.resize=true;
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    WindowState* state=reinterpret_cast<WindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        state=static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        state->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(state) switch(message) {
    case WM_CLOSE: state->close=true;return 0; /* Keep HWND alive until DXGI is destroyed. */
    case WM_DESTROY: state->close=true;return 0; /* Never post WM_QUIT for the game. */
    case WM_SIZE: Size(*state);return 0;
    case WM_ACTIVATE: {
        const bool active=LOWORD(w)!=WA_INACTIVE;
        if(active && !state->active)++state->activations;
        state->active=active;return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if(w==VK_ESCAPE){state->close=true;return 0;}
        if((w==VK_F11 || (message==WM_SYSKEYDOWN && w==VK_RETURN && (l&(1L<<29)))) && !(l&(1L<<30))) {
            state->toggle=true;return 0;
        }
        if(w==VK_SPACE && !(l&(1L<<30))){state->palette=(state->palette+1)%3;return 0;}
        if(message==WM_KEYDOWN && SceneKey(*state,w,(l&(1L<<30))!=0))return 0;
        break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);return 0; }
    case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(window,message,w,l);
}
bool SetStyle(HWND window,LONG_PTR style)
{
    SetLastError(0);const LONG_PTR previous=SetWindowLongPtrW(window,GWL_STYLE,style);
    return previous!=0 || GetLastError()==0;
}
int32_t WindowError()
{
    const DWORD error=GetLastError();
    return error?static_cast<int32_t>(HRESULT_FROM_WIN32(error)):KICH_R4_E_STATE;
}
bool Toggle(WindowState& s)
{
    if(!s.borderless) {
        s.placement.length=sizeof(s.placement);
        if(!GetWindowPlacement(s.window,&s.placement))return false;
        s.style=GetWindowLongPtrW(s.window,GWL_STYLE);
        MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);
        if(!GetMonitorInfoW(MonitorFromWindow(s.window,MONITOR_DEFAULTTONEAREST),&monitor))return false;
        if(!SetStyle(s.window,WS_POPUP|WS_VISIBLE))return false;
        if(!SetWindowPos(s.window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,
            monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
            SWP_NOOWNERZORDER|SWP_FRAMECHANGED))return false;
        s.borderless=true;
    } else {
        if(!SetStyle(s.window,s.style) || !SetWindowPlacement(s.window,&s.placement) ||
            !SetWindowPos(s.window,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED))return false;
        s.borderless=false;
    }
    ++s.mode_changes;Size(s);return true;
}
bool MakeWindow(WindowState& s,KichR4LogFn log)
{
    s.instance=GetModuleHandleW(NULL);
    WNDCLASSEXW cls={};cls.cbSize=sizeof(cls);cls.lpfnWndProc=WindowProc;
    cls.hInstance=s.instance;cls.hCursor=LoadCursorW(NULL,MAKEINTRESOURCEW(32512));cls.lpszClassName=class_name;
    if(!RegisterClassExW(&cls)){Write(log,"[R4 bridge %s] window class failed: Win32=%lu",Stage(s),GetLastError());return false;}
    s.registered=true;
    RECT r={0,0,960,540};
    if(!AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0))return false;
    const int w=r.right-r.left,h=r.bottom-r.top;
    const int x=(GetSystemMetrics(SM_CXSCREEN)-w)/2,y=(GetSystemMetrics(SM_CYSCREEN)-h)/2;
    s.window=CreateWindowExW(0,class_name,s.level?level_title:s.geometry_mode?geometry_title:window_title,WS_OVERLAPPEDWINDOW,x,y,w,h,NULL,NULL,s.instance,&s);
    if(!s.window){Write(log,"[R4 bridge %s] window create failed: Win32=%lu",Stage(s),GetLastError());return false;}
    Size(s);
    return true;
}
bool Pump(void* context)
{
    WindowState& s=*static_cast<WindowState*>(context);MSG message;
    while(PeekMessageW(&message,s.window,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT){s.quit=true;s.quit_code=static_cast<int>(message.wParam);s.close=true;break;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    return !s.close;
}
uint32_t LevelKeys()
{
    const int keys[]={'W','S','A','D','E','Q',VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN,VK_SHIFT,VK_CONTROL};
    uint32_t result=0;for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i)if(GetAsyncKeyState(keys[i])&0x8000)result|=1u<<i;
    return result;
}
void RunWindow(WindowState& s,KichR4LogFn log)
{
    if(!MakeWindow(s,log)){s.session.Record("create window",KICH_R4_E_STATE);return;}
    if(s.session.Start(s.window,s.width,s.height)<0)return;
    if(s.geometry_mode && s.geometry.Start(s.session)<0)return;
    if(s.level){ShowWindow(s.window,SW_SHOWNORMAL);UpdateWindow(s.window);SetForegroundWindow(s.window);
        s.level->log=log;s.level->keep_going=Pump;s.level->progress_context=&s;
        if(s.level->Start(s.session,s.level_name)!=KICH_R4_OK)return;}
    Write(log,"[R4 bridge %s] create: %ux%u; hardware FL11_0; RGBA8; vsync=1; windowed",Stage(s),s.width,s.height);
    Write(log,"[R4 bridge %s] controls: resize / minimize / Alt+Tab; F11 or Alt+Enter = borderless; Space = color; Esc or close = continue game",Stage(s));
    if(s.geometry_mode)log("[R4 bridge B4.1] geometry ready: VS/PS SM5, vertex/index/constant buffers, input layout; keys 1 triangle / 2 cubes / D depth / C cull / P pause / arrows camera / +/- zoom / Home reset camera");
    if(s.level)log("[R4 bridge B4.2] controls: WASD fly / Q down / E up / arrows look / Shift fast / Ctrl slow / Home overview / T textures / C cull / Z depth; F11 twice tests both window modes");
    ShowWindow(s.window,SW_SHOWNORMAL);UpdateWindow(s.window);SetForegroundWindow(s.window);
    const DWORD start=GetTickCount();
    DWORD previous_tick=start;
    bool first=true,paused=false,hidden=false;
    uint32_t last_width=s.width,last_height=s.height,last_modes=0,last_activations=s.activations;
    uint32_t last_controls=UINT32_MAX;
    DWORD last_level_log=0;
    while(!s.close) {
        /* Process only the test window: the game loop/renderer are not ready.
         * WM_QUIT is returned regardless of HWND filter; preserve it on exit. */
        if(!Pump(&s))break;
        if(s.toggle) {
            s.toggle=false;
            if(!Toggle(s)){s.session.Record("change window mode",WindowError());break;}
        }
        if(s.resize) {
            s.resize=false;
            const int32_t hr=s.session.Resize(s.width,s.height);if(hr<0)break;
            if(s.width && s.height && (s.width!=last_width || s.height!=last_height)) {
                Write(log,"[R4 bridge %s] resize: %ux%u",Stage(s),s.width,s.height);last_width=s.width;last_height=s.height;
            }
        }
        if(last_modes!=s.mode_changes) {
            Write(log,"[R4 bridge %s] window mode=%s; changes=%u",Stage(s),s.borderless?"borderless":"windowed",s.mode_changes);
            last_modes=s.mode_changes;
        }
        const bool pause=!s.active || !s.width || !s.height;
        if(pause!=paused || last_activations!=s.activations) {
            Write(log,"[R4 bridge %s] %s; activations=%u",Stage(s),pause?"paused (inactive/minimized)":"active",s.activations);
            paused=pause;last_activations=s.activations;
        }
        /* A filtered pump can leave unrelated game messages queued; sleeping
         * avoids a busy wait on QS_ALLINPUT while that queue stays nonempty. */
        const DWORD now=GetTickCount(),elapsed=now-previous_tick;previous_tick=now;
        if(pause){Sleep(50);continue;}
        if(s.level){
            if(s.level->camera.Advance(LevelKeys(),static_cast<float>(elapsed)*.001f))++s.level->camera_steps;
            if(last_controls!=s.level->revision || now-last_level_log>=2000){const char* culls[]={"back","front","none"};
                Write(log,"[R4 bridge B4.2] camera=(%.1f,%.1f,%.1f) yaw=%.2f pitch=%.2f; textures=%s depth=%s cull=%s; visible=%u culled=%u",
                    s.level->camera.position.x,s.level->camera.position.y,s.level->camera.position.z,s.level->camera.yaw,s.level->camera.pitch,
                    s.level->textured?"on":"off",s.level->depth?"on":"off",culls[s.level->cull],s.level->visible,s.level->culled);
                wchar_t title[256];const wchar_t* wide_culls[]={L"back",L"front",L"none"};
                swprintf(title,sizeof(title)/sizeof(title[0]),L"Kich-Ray DX11 B4.2 | textures %ls | depth %ls | cull %ls | meshes %u | WASD + arrows, Shift, Home, T/C/Z, F11, Esc",
                    s.level->textured?L"on":L"off",s.level->depth?L"on":L"off",wide_culls[s.level->cull],s.level->visible);
                SetWindowTextW(s.window,title);last_controls=s.level->revision;last_level_log=now;
            }
        }
        if(s.geometry_mode){
            if(!s.controls.paused)s.controls.angle=fmodf(s.controls.angle+static_cast<float>(elapsed>100?100:elapsed)*.0008f,6.2831853f);
            if(last_controls!=s.controls_revision){const char* culls[]={"back","front","none"};
                Write(log,"[R4 bridge B4.1] scene=%s depth=%s cull=%s rotation=%s; camera yaw=%.2f pitch=%.2f distance=%.2f",
                    s.controls.mode==1?"triangle":"cubes",s.controls.depth?"on":"off",culls[s.controls.cull],
                    s.controls.paused?"paused":"running",s.controls.yaw,s.controls.pitch,s.controls.distance);
                const wchar_t* wide_culls[]={L"back",L"front",L"none"};
                wchar_t title[256];
                swprintf(title,sizeof(title)/sizeof(title[0]),L"Kich-Ray DX11 B4.1 | %ls | depth %ls | cull %ls | rotation %ls | 1/2 scene, D/C/P, arrows, +/-",
                    s.controls.mode==1?L"triangle":L"cubes",s.controls.depth?L"on":L"off",wide_culls[s.controls.cull],
                    s.controls.paused?L"paused":L"running");
                SetWindowTextW(s.window,title);
                last_controls=s.controls_revision;
            }
        }
        float phase=static_cast<float>((GetTickCount()-start)%4000)/2000.f;
        if(phase>1.f)phase=2.f-phase;
        KichR4FrameColor color={sizeof(color),{0.03f,0.15f+phase*0.12f,0.30f+phase*0.12f,1.f}};
        if(s.palette==1){color.rgba[0]=0.25f+phase*0.15f;color.rgba[1]=0.06f;color.rgba[2]=0.12f;}
        else if(s.palette==2){color.rgba[0]=0.06f;color.rgba[1]=0.25f+phase*0.15f;color.rgba[2]=0.10f;}
        int32_t hr;
        if(s.geometry_mode || s.level){hr=s.session.BeginClear(color);
            if(hr==KICH_R4_OK)hr=s.level?s.level->Draw(s.session,s.width,s.height):s.geometry.Draw(s.session,s.controls,s.width,s.height);
            if(hr==KICH_R4_OK)hr=s.session.Present();
        }else hr=s.session.Draw(color);
        if(hr<0)break;
        const bool occluded=hr==KICH_R4_FRAME_OCCLUDED;
        if(occluded!=hidden){Write(log,"[R4 bridge %s] target %s",Stage(s),occluded?"occluded; waiting":"visible");hidden=occluded;}
        if(hr>0){Sleep(100);continue;}
        if(first){Write(log,"[R4 bridge %s] first DX11 %sframe presented",Stage(s),s.level?"level ":s.geometry_mode?"geometry ":"");first=false;}
    }
}
void GuardRun(WindowState& s,KichR4LogFn log)
{
#if defined(_MSC_VER)
    __try {RunWindow(s,log);}
    __except(EXCEPTION_EXECUTE_HANDLER) {s.session.Record("window/frame API exception",static_cast<int32_t>(GetExceptionCode()));}
#else
    RunWindow(s,log);
#endif
}
int32_t Stop(WindowState& s,KichR4FrameStats* stats,KichR4GeometryReport* geometry_report)
{
    if(!s.geometry_mode && !s.level)return s.session.Stop(stats);
    const int32_t hr=s.level?s.level->bridge.Stop(s.session,geometry_report):s.geometry.Stop(s.session,geometry_report);
    if(hr==0 && stats && geometry_report)*stats=geometry_report->frame;
    return hr;
}
int32_t GuardStop(WindowState& s,KichR4FrameStats* stats,KichR4GeometryReport* geometry_report)
{
#if defined(_MSC_VER)
    __try {return Stop(s,stats,geometry_report);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return s.session.Record("cleanup exception",static_cast<int32_t>(GetExceptionCode()));}
#else
    return Stop(s,stats,geometry_report);
#endif
}
bool Fetch(KichR4GetFrameAPIFn get,KichR4FrameAPI& api,KichR4LogFn log,const char* stage)
{
    int32_t hr=KICH_R4_E_STATE;
#if defined(_MSC_VER)
    __try {hr=get(KICH_R4_FRAME_ABI,&api,sizeof(api));}
    __except(EXCEPTION_EXECUTE_HANDLER) {hr=static_cast<int32_t>(GetExceptionCode());}
#else
    hr=get(KICH_R4_FRAME_ABI,&api,sizeof(api));
#endif
    if(hr!=KICH_R4_OK || !KichR4FrameSession::ValidAPI(api)) {
        Write(log,"[R4 bridge %s] frame ABI/table rejected: HRESULT=0x%08X; test skipped",stage,static_cast<uint32_t>(hr));return false;
    }
    return true;
}
bool FetchGeometry(void* module,WindowState& s,KichR4LogFn log)
{
    const KichR4GetResourceAPIFn resource=reinterpret_cast<KichR4GetResourceAPIFn>(
        GetProcAddress(static_cast<HMODULE>(module),KICH_R4_EXPORT_RESOURCES));
    const KichR4GetGeometryAPIFn geometry=reinterpret_cast<KichR4GetGeometryAPIFn>(
        GetProcAddress(static_cast<HMODULE>(module),KICH_R4_EXPORT_GEOMETRY));
    if(!resource || !geometry){log("[R4 bridge B4.1] geometry/resource export missing; use the B4.1 DLL; test skipped");return false;}
    int32_t hr=KICH_R4_E_STATE;
#if defined(_MSC_VER)
    __try
#endif
    {
        hr=resource(KICH_R4_RESOURCE_ABI,&s.geometry.resources,sizeof(s.geometry.resources));
        if(hr==0)hr=geometry(KICH_R4_GEOMETRY_ABI,&s.geometry.api,sizeof(s.geometry.api));
    }
#if defined(_MSC_VER)
    __except(EXCEPTION_EXECUTE_HANDLER){hr=static_cast<int32_t>(GetExceptionCode());}
#endif
    if(hr!=0 || !KichR4GeometrySession::ValidResources(s.geometry.resources) || !KichR4GeometrySession::ValidAPI(s.geometry.api)){
        Write(log,"[R4 bridge B4.1] geometry ABI/table rejected: HRESULT=0x%08X; test skipped",static_cast<uint32_t>(hr));return false;}
    return true;
}
bool FetchLevel(KichR4GetLevelAPIFn get,KichR4LevelAPI& api,KichR4LogFn log)
{
    int32_t hr=KICH_R4_E_STATE;
#if defined(_MSC_VER)
    __try {hr=get(KICH_R4_LEVEL_ABI,&api,sizeof(api));}
    __except(EXCEPTION_EXECUTE_HANDLER){hr=static_cast<int32_t>(GetExceptionCode());}
#else
    hr=get(KICH_R4_LEVEL_ABI,&api,sizeof(api));
#endif
    if(hr!=0 || !KichR4LevelSession::ValidAPI(api)){Write(log,"[R4 bridge B4.2] level ABI/table rejected: HRESULT=0x%08X",static_cast<uint32_t>(hr));return false;}
    return true;
}
bool PrepareLevel(void* module,WindowState& s,const KichR4LevelFiles* files,KichR4LogFn log)
{
    if(!files || !files->open || !files->close){log("[R4 bridge B4.2] engine filesystem adapter missing; test skipped");return false;}
    const KichR4GetLevelAPIFn get=reinterpret_cast<KichR4GetLevelAPIFn>(GetProcAddress(static_cast<HMODULE>(module),KICH_R4_EXPORT_LEVEL));
    if(!get){log("[R4 bridge B4.2] level export missing; update xrRender_R4.dll; test skipped");return false;}
    try{s.level=new KichR4LevelSession();}catch(...){log("[R4 bridge B4.2] cannot allocate viewer state; test skipped");return false;}
    if(!FetchLevel(get,s.level->api,log)){delete s.level;s.level=NULL;return false;}
    s.level->bridge=s.geometry;s.level->files=*files;return true;
}
}

bool KichR4_CheckFrameBridge(void* module,KichR4LogFn log,bool geometry,const char* level,const KichR4LevelFiles* files)
{
    Write(log,"[R4 bridge %s] begin: autonomous DX11 %stest before R1/R2",level?"B4.2":geometry?"B4.1":"B3",level?"level ":geometry?"geometry ":"frame ");
    const KichR4GetFrameAPIFn get=reinterpret_cast<KichR4GetFrameAPIFn>(
        GetProcAddress(static_cast<HMODULE>(module),KICH_R4_EXPORT_FRAME));
    if(!get){Write(log,"[R4 bridge %s] frame export missing; update xrRender_R4.dll; test skipped",level?"B4.2":geometry?"B4.1":"B3");return true;}
    WindowState state={};
    state.level_name=level;state.geometry_mode=geometry && !level;state.controls=kich_r4_scene::Defaults();
    if(!Fetch(get,state.session.api,log,Stage(state)))return true;
    if((geometry || level) && !FetchGeometry(module,state,log))return true;
    if(level && !PrepareLevel(module,state,files,log))return true;
    GuardRun(state,log);
    KichR4FrameStats final_stats={};
    KichR4GeometryReport geometry_report={};
    const bool had_device=state.session.device!=0;
    const int32_t stopped=GuardStop(state,&final_stats,&geometry_report);
    if(stopped<0 && state.session.device)GuardStop(state,NULL,NULL); /* One cleanup retry, no frame retry. */
    if(state.window && IsWindow(state.window))DestroyWindow(state.window);
    if(state.registered)UnregisterClassW(class_name,state.instance);
    if(state.quit)PostQuitMessage(state.quit_code);
    if(had_device && stopped==KICH_R4_OK) {
        Write(log,"[R4 bridge %s] destroy: presented=%u attempts=%u clears=%u resizes=%u occlusions=%u; views=%u/%u; flags=0x%X; activations=%u mode_changes=%u",
            Stage(state),final_stats.presents,final_stats.present_attempts,final_stats.clears,final_stats.resizes,final_stats.occlusions,
            final_stats.views_created,final_stats.views_released,final_stats.flags,state.activations,state.mode_changes);
        if(final_stats.struct_size!=sizeof(final_stats) || final_stats.flags || final_stats.views_created!=final_stats.views_released)
            state.session.Record("final ownership report",KICH_R4_E_STATE);
        if(geometry || level){const KichR4GeometryStats& g=geometry_report.geometry;const KichR4ResourceStats& r=geometry_report.resources;
            if(!level)Write(log,"[R4 bridge B4.1] geometry: draws=%u triangles=%u updates=%u; pipelines=%u/%u depth=%u/%u resources=%u/%u live=%u; triangle_frames=%u cube_frames=%u",
                g.draw_calls,g.triangles,g.buffer_updates,g.pipelines_created,g.pipelines_released,g.depth_created,g.depth_released,
                r.created,r.released,r.live_resources,state.geometry.triangle_frames,state.geometry.cube_frames);
            else Write(log,"[R4 bridge B4.2] geometry: draws=%u triangles=%u updates=%u; pipelines=%u/%u depth=%u/%u resources=%u/%u live=%u; level_frames=%u DDS=%u fallback=%u camera_steps=%u; submitted_draws=%llu submitted_triangles=%llu",
                g.draw_calls,g.triangles,g.buffer_updates,g.pipelines_created,g.pipelines_released,g.depth_created,g.depth_released,
                r.created,r.released,r.live_resources,state.level->frames,state.level->textures_loaded,state.level->texture_fallbacks,state.level->camera_steps,
                static_cast<unsigned long long>(state.level->submitted_draws),static_cast<unsigned long long>(state.level->submitted_triangles));
            if(geometry_report.struct_size!=sizeof(geometry_report) || g.struct_size!=sizeof(g) || r.struct_size!=sizeof(r) ||
                r.live_resources || r.payload_bytes || r.created!=r.released || g.pipelines_created!=g.pipelines_released ||
                g.depth_created!=g.depth_released)state.session.Record("final resource ownership report",KICH_R4_E_STATE);
        }
    }
    if(state.session.failure) {
        Write(log,"[R4 bridge %s] FAIL: %s; HRESULT=0x%08X; removed_reason=0x%08X; continuing R1/R2",
            Stage(state),state.session.step,static_cast<uint32_t>(state.session.failure),static_cast<uint32_t>(final_stats.device_removed_reason));
        state.geometry.shader_error[sizeof(state.geometry.shader_error)-1]=0;
        if(state.geometry.shader_error[0])log(state.geometry.shader_error);
        if(state.level){if(!state.level->detail.empty())Write(log,"[R4 bridge B4.2] detail: %s",state.level->detail.c_str());
            state.level->bridge.shader_error[sizeof(state.level->bridge.shader_error)-1]=0;
            if(state.level->bridge.shader_error[0])log(state.level->bridge.shader_error);}
    } else if(final_stats.presents && state.session.rendered && stopped==KICH_R4_OK){
        if(level){
            if(state.level->frames && state.level->textures_loaded && geometry_report.geometry.draw_calls)
                log("[R4 bridge B4.2] PASS: level geometry and DDS textures drawn; resources/depth/target/device released; continuing R1/R2");
            else log("[R4 bridge B4.2] INCOMPLETE: no visible geometry or no source DDS texture loaded; continuing R1/R2");
        }else if(!geometry)log("[R4 bridge B3] PASS: DX11 clear/Present completed; target/device released; continuing R1/R2");
        else if(state.geometry.triangle_frames && state.geometry.cube_frames && geometry_report.geometry.draw_calls)
            log("[R4 bridge B4.1] PASS: triangle and cubes drawn; geometry/depth/target/device released; continuing R1/R2");
        else log("[R4 bridge B4.1] INCOMPLETE: exercise both 1 (triangle) and 2 (cubes); continuing R1/R2");
    }else Write(log,"[R4 bridge %s] INCOMPLETE: no successful DX11 Present; continuing R1/R2",Stage(state));
    const bool can_unload=state.session.device==0;
    delete state.level;
    return can_unload;
}
