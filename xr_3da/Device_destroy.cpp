#include "stdafx.h"

#include "render.h"
#include "IGame_Persistent.h"
#include "KichRenderDevice.h"

void CRenderDevice::_Destroy(BOOL bKeepTextures)
{
	DU.OnDeviceDestroy();
	m_WireShader.destroy();
	m_SelectionShader.destroy();

	// before destroy
	b_is_Ready = FALSE;
	Statistic->OnDeviceDestroy();
	::Render->destroy();
	KichRenderDevice().OnResourcesDestroy(bKeepTextures != FALSE);
	KichRenderDevice().OnDeviceDestroy();

	Memory.mem_compact();
}

void CRenderDevice::Destroy(void)
{
	if (!b_is_Ready)
		return;

	Log("Destroying Direct3D...");

	ShowCursor(TRUE);
	KichRenderDevice().ValidateHW();

	_Destroy(FALSE);

	KichRenderDevice().ReleaseResources();

	// real destroy
	KichRenderDevice().DestroyHW();

	seqRender.R.clear();
	seqAppActivate.R.clear();
	seqAppDeactivate.R.clear();
	seqAppStart.R.clear();
	seqAppEnd.R.clear();
	seqFrame.R.clear();
	seqFrameMT.R.clear();
	seqDeviceReset.R.clear();
	seqParallel.clear();

	xr_delete(Statistic);
}

#include "IGame_Level.h"
#include "CustomHUD.h"
extern BOOL bNeed_re_create_env;
void CRenderDevice::Reset(bool precache)
{
	KichRenderDevice().TraceResetReferences(true);
	bool b_16_before = (float)dwWidth / (float)dwHeight > (1024.0f / 768.0f + 0.01f);

	ShowCursor(TRUE);
	u32 tm_start = TimerAsync();
	if (g_pGamePersistent)
	{
		//.		g_pGamePersistent->Environment().OnDeviceDestroy();
	}

	KichRenderDevice().BeginResourcesReset();
	Memory.mem_compact();
	KichRenderDeviceSize size;
	KichRenderDevice().ResetHW(m_hWnd, size);
	dwWidth = size.width;
	dwHeight = size.height;
	fWidth_2 = float(dwWidth / 2);
	fHeight_2 = float(dwHeight / 2);
	KichRenderDevice().EndResourcesReset();

	if (g_pGamePersistent)
	{
		//.		g_pGamePersistent->Environment().OnDeviceCreate();
		bNeed_re_create_env = TRUE;
	}
	_SetupStates();
	if (precache)
		PreCache(20);
	u32 tm_end = TimerAsync();
	Msg("*** RESET [%d ms]", tm_end - tm_start);

#ifndef DEDICATED_SERVER
	ShowCursor(FALSE);
#endif

	seqDeviceReset.Process(rp_DeviceReset);

	bool b_16_after = (float)dwWidth / (float)dwHeight > (1024.0f / 768.0f + 0.01f);
	if (b_16_after != b_16_before && g_pGameLevel && g_pGameLevel->pHUD)
		g_pGameLevel->pHUD->OnScreenRatioChanged();

	KichRenderDevice().TraceResetReferences(false);
}
