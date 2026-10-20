#include "stdafx.h"
#include "KichR4Game.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "XR_IOConsole.h"

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
    KichR4_GamePrepare(input);
#endif
}
