#include "stdafx.h"
#include "RenderSnapshotEngine.h"
#include "RenderSnapshot.h"
#if !defined(_EDITOR)
#include "Render.h"
#include "KichR4Native.h"
#include "KichR4BuildIdentity.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#include "XR_IOConsole.h"
#include "CustomHUD.h"
#include "ResourceManager.h"
#include "RenderSnapshotD3D9.h"
#include <stdio.h>

namespace {
uint64_t snapshot_device_generation=1,snapshot_level_generation=0;
const void* snapshot_last_level=NULL;char snapshot_last_level_name[128]={};bool snapshot_last_level_loaded=false;
void SnapshotMatrix(const char* key,const Fmatrix& matrix){
    char value[512];size_t at=0;for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){
        int written=snprintf(value+at,sizeof(value)-at,"%s%.9g",at?",":"",matrix.m[row][column]);
        if(written<0 || (size_t)written>=sizeof(value)-at){value[sizeof(value)-1]=0;RenderSnapshot_Text("CAMERA",key,"SERIALIZATION_OVERFLOW");return;}at+=(size_t)written;}
    RenderSnapshot_Text("CAMERA",key,value);
}
}
void RenderSnapshot_EngineDeviceEvent(const char* reason){RenderSnapshot_Abort(reason);++snapshot_device_generation;}
void RenderSnapshot_EngineBegin(){
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    const bool level_loaded=g_pGameLevel && g_pGameLevel->bReady;
    char level_name[128];strncpy_s(level_name,g_pGameLevel?g_pGameLevel->name().c_str():"NONE",_TRUNCATE);
    if(snapshot_last_level!=g_pGameLevel || strcmp(snapshot_last_level_name,level_name) || snapshot_last_level_loaded!=level_loaded){
        ++snapshot_level_generation;snapshot_last_level=g_pGameLevel;snapshot_last_level_loaded=level_loaded;
        strncpy_s(snapshot_last_level_name,level_name,_TRUNCATE);RenderSnapshot_Event("LEVEL",level_name,"GENERATION_CHANGED","OBSERVED");}
    RenderSnapshot_FrameIDs(UINT64_MAX,UINT64_MAX,snapshot_level_generation,snapshot_device_generation);
    const bool menu=g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query();
    const char* screen=RenderSnapshot_UIScreen();
    if(menu || !strcmp(screen,"MENU"))screen=level_loaded?"MENU":"START_MENU";
    else if(!strcmp(screen,"DIALOG"))screen="OTHER";
    else if(!level_loaded && !strcmp(screen,"GAME"))screen="START_MENU";
    RenderSnapshot_UIState(screen,RenderSnapshot_UIRoot(),RenderSnapshot_UIInputOwner(),level_loaded,Console && Console->bVisible,Device.Paused()!=0);
    if(!RenderSnapshot_Wanted())return;
    RenderSnapshotInfo info={};info.frame=Device.dwFrame;info.width=Device.dwWidth;info.height=Device.dwHeight;
    info.console_visible=Console && Console->bVisible;info.paused=Device.Paused();info.hud=psHUD_Flags.test(HUD_DRAW)?1:0;
    CopyMemory(info.position,&Device.vCameraPosition.x,12);CopyMemory(info.direction,&Device.vCameraDirection.x,12);
    info.fov=Device.fFOV;info.time=Device.fTimeGlobal;
    const bool native=KichR4_NativeActive();
    strcpy_s(info.renderer,native?"R4":Render && Render->get_generation()==IRender_interface::GENERATION_R2?"R2":"R1");
    strcpy_s(info.api,native?"D3D11":"D3D9");
    strcpy_s(info.scene,Device.dwPrecacheFrame || !g_loading_events.empty()?"LOADING":menu || !g_pGameLevel || !g_pGameLevel->bReady?"MENU":"GAME");
    strncpy_s(info.level,g_pGameLevel?g_pGameLevel->name().c_str():"NONE",_TRUNCATE);
    if(g_pGamePersistent){CEnvironment& env=g_pGamePersistent->Environment();
        strncpy_s(info.weather,env.CurrentWeatherName.size()?env.CurrentWeatherName.c_str():"UNKNOWN",_TRUNCATE);
        info.wind=env.wind_strength_factor;info.game_time=env.SnapshotGameTime();CopyMemory(info.sun,&env.CurrentEnv.sun_dir.x,12);}
    const KichR4BuildInfo build=KichR4_LocalBuildInfo();strcpy_s(info.build,build.revision);strcpy_s(info.source_id,build.source_id);strcpy_s(info.shader_id,build.shader_id);
    string_path directory;
    FS.update_path(directory,"$logs$","");
    strncpy_s(info.directory,directory,_TRUNCATE);
    strcpy_s(info.adapter,"NOT_AVAILABLE");
    if(!native && HW.pD3D){D3DADAPTER_IDENTIFIER9 adapter={};if(SUCCEEDED(HW.pD3D->GetAdapterIdentifier(HW.DevAdapter,0,&adapter)))strncpy_s(info.adapter,adapter.Description,_TRUNCATE);}
    RenderSnapshot_Begin(info);
    RenderSnapshot_U64("FRAME","engine_frame",Device.dwFrame);RenderSnapshot_U64("FRAME","level_generation",snapshot_level_generation);
    RenderSnapshot_U64("FRAME","device_generation",snapshot_device_generation);RenderSnapshot_U64("FRAME","engine_time_ms",Device.dwTimeGlobal);
    RenderSnapshot_U64("UI_STATE","level_exists",g_pGameLevel?1:0);RenderSnapshot_U64("UI_STATE","level_ready",level_loaded?1:0);
    RenderSnapshot_U64("UI_STATE","device_active",Device.b_is_Active?1:0);RenderSnapshot_U64("UI_STATE","device_ready",Device.b_is_Ready?1:0);
    RenderSnapshot_U64("UI_STATE","menu_ppui_requested",menu?1:0);RenderSnapshot_Text("UI_STATE","observed_screen",screen);
    RenderSnapshot_Text("UI_STATE","root_window",RenderSnapshot_UIRoot());RenderSnapshot_Text("UI_STATE","input_owner",RenderSnapshot_UIInputOwner());
    RenderSnapshot_Text("UI_STATE","input_owner_identity","PROCESS_HANDLE_NOT_PERSISTENT");
    RenderSnapshot_Text("UI_STATE","cursor_status","AWAITING_UI_CURSOR_RENDER_CALLBACK");
    RenderSnapshot_Text("CAMERA","matrix_layout","Fmatrix.m row-major memory; engine row-vector convention");
    RenderSnapshot_Value("CAMERA","aspect",Device.fASPECT);RenderSnapshot_Value("CAMERA","frame_delta_seconds",Device.fTimeDelta);
    SnapshotMatrix("view",Device.mView);SnapshotMatrix("world_projection",Device.mProject);SnapshotMatrix("world_view_projection",Device.mFullTransform);
    RenderSnapshot_Text("CAMERA","projection_sample_point","GRAPHICAL_BEGIN_BEFORE_BACKEND_FRAME");
    RenderSnapshot_Text("TIMINGS","d3d9_gpu_status",native?"R4_QUERY_PROVIDER":"NOT_INSTRUMENTED");
    RenderSnapshot_Value("SETTINGS","details_enabled",psDeviceFlags.test(rsDetails)?1:0);
    if(Console){float value=0,minimum=0,maximum=0;Console->GetFloat("hud_fov",value,minimum,maximum);
        RenderSnapshot_Value("CAMERA","configured_hud_fov_multiplier",value);RenderSnapshot_Text("CAMERA","hud_projection_source","DRAW_PASS_TELEMETRY_WHEN_BOUND");
        Console->GetFloat("r__detail_density",value,minimum,maximum);RenderSnapshot_Value("SETTINGS","detail_density",value);
        Console->GetFloat("r__detail_radius",value,minimum,maximum);RenderSnapshot_Value("SETTINGS","detail_radius",value);}
#endif
}
void RenderSnapshot_EngineEnd(){
    if(!g_render_snapshot_active)return;
    RenderSnapshot_End("COMPLETE");Msg("[snapshot] %s",RenderSnapshot_LastPath());
}
void RenderSnapshot_D3D9Draw(void* vb,void* ib,uint32_t stride,uint32_t topology,uint32_t vertices,uint32_t first,uint32_t primitives,bool indexed,int32_t result){
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    if(!g_render_snapshot_active || !HW.pDevice)return;
    if(g_render_snapshot_console)RenderSnapshot_Context("CONSOLE","CONSOLE","CONSOLE_UI");
    RenderSnapshotDraw draw={};draw.struct_size=sizeof(draw);draw.submitted=result>=0;draw.topology=topology;draw.instance_count=1;
    const uint32_t count=topology==D3DPT_TRIANGLELIST?primitives*3:topology==D3DPT_TRIANGLESTRIP || topology==D3DPT_TRIANGLEFAN?primitives+2:
        topology==D3DPT_LINELIST?primitives*2:topology==D3DPT_LINESTRIP?primitives+1:primitives;
    draw.vertex_count=indexed?vertices:count;draw.vertex_count_known=1;draw.index_count=indexed?count:0;draw.start_index=first;draw.vb=reinterpret_cast<uintptr_t>(vb);draw.ib=reinterpret_cast<uintptr_t>(ib);
    if(!RenderSnapshot_Detailed()){RenderSnapshot_Draw(&draw);return;}
    IDirect3DVertexShader9* vs=NULL;IDirect3DPixelShader9* ps=NULL;
    const HRESULT vs_result=HW.pDevice->GetVertexShader(&vs),ps_result=HW.pDevice->GetPixelShader(&ps);draw.vs=reinterpret_cast<uintptr_t>(vs);draw.ps=reinterpret_cast<uintptr_t>(ps);
    if(Device.Resources){strncpy_s(draw.vertex_shader,SUCCEEDED(vs_result)?Device.Resources->SnapshotVSName(vs):"NOT_AVAILABLE",_TRUNCATE);strncpy_s(draw.pixel_shader,SUCCEEDED(ps_result)?Device.Resources->SnapshotPSName(ps):"NOT_AVAILABLE",_TRUNCATE);}
    _RELEASE(vs);_RELEASE(ps);
    const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,
        D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_CULLMODE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,
        D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILPASS,D3DRS_STENCILZFAIL,D3DRS_COLORWRITEENABLE,D3DRS_SCISSORTESTENABLE};
    const char* names[]={"ZENABLE","ZWRITE","ZFUNC","ALPHATEST","ALPHAFUNC","ALPHAREF","BLEND","SRC","DST","BLENDOP","CULL","STENCIL","STENCILFUNC","STENCILREF","READMASK","WRITEMASK","STENCILFAIL","STENCILPASS","STENCILZFAIL","COLORWRITE","SCISSOR"};
    size_t at=0;unsigned unknown=0;
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){DWORD value=0;const HRESULT hr=HW.pDevice->GetRenderState(keys[i],&value);if(FAILED(hr))++unknown;
        if(at<sizeof(draw.states)){if(SUCCEEDED(hr))at+=snprintf(draw.states+at,sizeof(draw.states)-at,"%s=%lu;",names[i],value);
            else at+=snprintf(draw.states+at,sizeof(draw.states)-at,"%s=NOT_AVAILABLE;",names[i]);}}
    if(at<sizeof(draw.states))snprintf(draw.states+at,sizeof(draw.states)-at,"stride=%u;unavailable=%u;enum=D3D9",stride,unknown);
    at=0;
    for(unsigned i=0;i<8;++i){IDirect3DBaseTexture9* texture=NULL;const HRESULT texture_result=HW.pDevice->GetTexture(i,&texture);draw.textures[i]=reinterpret_cast<uintptr_t>(texture);
        strncpy_s(draw.texture_names[i],SUCCEEDED(texture_result) && Device.Resources?Device.Resources->SnapshotTextureName(texture):"NOT_AVAILABLE",_TRUNCATE);_RELEASE(texture);
        DWORD values[6]={};const D3DSAMPLERSTATETYPE sampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER};
        bool known=true;for(unsigned j=0;j<6;++j)if(FAILED(HW.pDevice->GetSamplerState(i,sampler[j],&values[j])))known=false;
        if(at<sizeof(draw.samplers)){if(known)at+=snprintf(draw.samplers+at,sizeof(draw.samplers)-at,"s%u:UVW=%lu/%lu/%lu,min/mag/mip=%lu/%lu/%lu;",i,values[0],values[1],values[2],values[3],values[4],values[5]);
            else at+=snprintf(draw.samplers+at,sizeof(draw.samplers)-at,"s%u:NOT_AVAILABLE;",i);}
    }
    draw.states[sizeof(draw.states)-1]=draw.samplers[sizeof(draw.samplers)-1]=0;
    RenderSnapshot_D3D9Resources(HW.pDevice,stride);
    RenderSnapshot_Draw(&draw);
#else
    (void)vb;(void)ib;(void)stride;(void)topology;(void)vertices;(void)first;(void)primitives;(void)indexed;(void)result;
#endif
}
#endif
