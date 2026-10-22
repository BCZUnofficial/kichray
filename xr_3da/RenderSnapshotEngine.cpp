#include "stdafx.h"
#include "RenderSnapshot.h"
#if !defined(_EDITOR)
#include "KichR4Native.h"
#include "KichR4BuildIdentity.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#include "XR_IOConsole.h"
#include "CustomHUD.h"
#include "ResourceManager.h"
#include <stdio.h>

void RenderSnapshot_EngineBegin(){
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    if(!RenderSnapshot_Wanted())return;
    RenderSnapshotInfo info={};info.frame=Device.dwFrame;info.width=Device.dwWidth;info.height=Device.dwHeight;
    info.console_visible=Console && Console->bVisible;info.paused=Device.Paused();info.hud=psHUD_Flags.test(HUD_DRAW)?1:0;
    CopyMemory(info.position,&Device.vCameraPosition.x,12);CopyMemory(info.direction,&Device.vCameraDirection.x,12);
    info.fov=Device.fFOV;info.time=Device.fTimeGlobal;
    const bool native=KichR4_NativeActive();
    strcpy_s(info.renderer,native?"R4":Render && Render->get_generation()==IRender_interface::GENERATION_R2?"R2":"R1");
    strcpy_s(info.api,native?"D3D11":"D3D9");
    const bool menu=g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query();
    strcpy_s(info.scene,Device.dwPrecacheFrame || !g_loading_events.empty()?"LOADING":menu || !g_pGameLevel || !g_pGameLevel->bReady?"MENU":"GAME");
    strncpy_s(info.level,g_pGameLevel?g_pGameLevel->name().c_str():"NONE",_TRUNCATE);
    if(g_pGamePersistent){CEnvironment& env=g_pGamePersistent->Environment();
        strncpy_s(info.weather,env.CurrentWeatherName.size()?env.CurrentWeatherName.c_str():"UNKNOWN",_TRUNCATE);
        info.wind=env.wind_strength_factor;info.game_time=env.SnapshotGameTime();CopyMemory(info.sun,&env.CurrentEnv.sun_dir.x,12);}
    const KichR4BuildInfo build=KichR4_LocalBuildInfo();strcpy_s(info.build,build.revision);strcpy_s(info.source_id,build.source_id);strcpy_s(info.shader_id,build.shader_id);
    FS.update_path(info.directory,"$logs$","");
    strcpy_s(info.adapter,"NOT_AVAILABLE");
    if(!native && HW.pD3D){D3DADAPTER_IDENTIFIER9 adapter={};if(SUCCEEDED(HW.pD3D->GetAdapterIdentifier(HW.DevAdapter,0,&adapter)))strncpy_s(info.adapter,adapter.Description,_TRUNCATE);}
    RenderSnapshot_Begin(info);
    RenderSnapshot_Value("SETTINGS","details_enabled",psDeviceFlags.test(rsDetails)?1:0);
    if(Console){float value=0,minimum=0,maximum=0;Console->GetFloat("r__detail_density",value,minimum,maximum);RenderSnapshot_Value("SETTINGS","detail_density",value);
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
    for(unsigned i=0;i<8;++i){IDirect3DBaseTexture9* texture=NULL;HW.pDevice->GetTexture(i,&texture);draw.textures[i]=reinterpret_cast<uintptr_t>(texture);_RELEASE(texture);
        CTexture* named=RCache.get_ActiveTexture(i);if(named)strncpy_s(draw.texture_names[i],named->cName.c_str(),_TRUNCATE);
        DWORD values[6]={};const D3DSAMPLERSTATETYPE sampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER};
        bool known=true;for(unsigned j=0;j<6;++j)if(FAILED(HW.pDevice->GetSamplerState(i,sampler[j],&values[j])))known=false;
        if(at<sizeof(draw.samplers)){if(known)at+=snprintf(draw.samplers+at,sizeof(draw.samplers)-at,"s%u:UVW=%lu/%lu/%lu,min/mag/mip=%lu/%lu/%lu;",i,values[0],values[1],values[2],values[3],values[4],values[5]);
            else at+=snprintf(draw.samplers+at,sizeof(draw.samplers)-at,"s%u:NOT_AVAILABLE;",i);}
    }
    draw.states[sizeof(draw.states)-1]=draw.samplers[sizeof(draw.samplers)-1]=0;
    RenderSnapshot_Draw(&draw);
#else
    (void)vb;(void)ib;(void)stride;(void)topology;(void)vertices;(void)first;(void)primitives;(void)indexed;(void)result;
#endif
}
#endif
