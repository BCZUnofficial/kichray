#include "stdafx.h"
#include "KichR4Game.h"
#include "KichR4Native.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "XR_IOConsole.h"
#include "Environment.h"

void KichR4_GamePrepareEngineFrame()
{
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    if(!KichR4_GameWindowed())return;
    KichR4GameFrame input={};
    input.window=Device.m_hWnd;input.width=Device.dwWidth;input.height=Device.dwHeight;
    input.engine_frame=Device.dwFrame;
    /* Fmatrix::mul(project,view) stores the row-vector view*projection matrix.
     * Both old DX9 and the SM5 shader use D3D clip depth [0,w]; no transpose. */
    memcpy(input.view_projection,Device.mFullTransform.m,sizeof(input.view_projection));
    input.position[0]=Device.vCameraPosition.x;input.position[1]=Device.vCameraPosition.y;input.position[2]=Device.vCameraPosition.z;
    input.active=Device.b_is_Active!=FALSE;
    const bool menu=g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive();
    input.allowed=g_pGameLevel && g_pGameLevel->bReady && g_pGameLevel->CurrentViewEntity() &&
        !Device.dwPrecacheFrame && g_loading_events.empty() && !Device.Paused() && !menu && !(Console && Console->bVisible);
    input.toggle_down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    input.debug_down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(g_pGamePersistent){const CEnvDescriptorMixer& env=g_pGamePersistent->Environment().CurrentEnv;
        input.lighting.valid=1;
        memcpy(input.lighting.sun_direction,&env.sun_dir.x,12);memcpy(input.lighting.sun_color,&env.sun_color.x,12);
        memcpy(input.lighting.ambient,&env.ambient.x,12);memcpy(input.lighting.hemi,&env.hemi_color.x,12);
        memcpy(input.lighting.fog,&env.fog_color.x,12);
    }
    KichR4_CaptureLighting(input.position,input.lighting);
    KichR4_GamePrepare(input);
#endif
}

void KichR4_NativePostprocess(float*);
void KichR4_NativePresentEngineFrame(bool loading)
{
    KichR4GameFrame input={};input.window=Device.m_hWnd;input.width=Device.dwWidth;input.height=Device.dwHeight;input.engine_frame=Device.dwFrame;
    CopyMemory(input.view_projection,Device.mFullTransform.m,64);CopyMemory(input.position,&Device.vCameraPosition.x,12);input.active=Device.b_is_Active!=FALSE;
    input.prepare_ms=static_cast<float>(KichR4_NativeQueueTime());input.time=Device.fTimeGlobal;KichR4_NativePostprocess(input.postprocess);input.allowed=!loading && g_pGameLevel && g_pGameLevel->bReady && !(g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query());input.debug_down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    const int path=loading?0:input.allowed?2:1;static int previous_path=-1;
    if(path!=previous_path){previous_path=path;Msg("[R4 native B5] output path=%s level_ready=%u precache=%u loading_events=%u",
        loading?"loading/UI":input.allowed?"game":"menu/UI",g_pGameLevel && g_pGameLevel->bReady?1u:0u,Device.dwPrecacheFrame,static_cast<u32>(g_loading_events.size()));}
    input.lighting.valid=1;input.lighting.sun_direction[1]=-1;input.lighting.ambient[0]=input.lighting.ambient[1]=input.lighting.ambient[2]=.2f;
    if(g_pGamePersistent){const CEnvDescriptorMixer& env=g_pGamePersistent->Environment().CurrentEnv;
        CopyMemory(input.lighting.sun_direction,&env.sun_dir.x,12);CopyMemory(input.lighting.sun_color,&env.sun_color.x,12);CopyMemory(input.lighting.ambient,&env.ambient.x,12);
        CopyMemory(input.lighting.hemi,&env.hemi_color.x,12);CopyMemory(input.lighting.fog,&env.fog_color.x,12);}
    KichR4_CaptureLighting(input.position,input.lighting);
    R_ASSERT2(KichR4_NativePresent(input),"R4 native frame failed; see log. Native mode has no DX9 frame to fall back to.");
}
