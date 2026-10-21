// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "xrXRC.h"
#include "KichR4Probe.h"
#include "KichR4Game.h"
#include "KichR4Native.h"

#ifndef DEDICATED_SERVER
#include "KichR4LevelFS.inl"
static void KichR4_WriteProbeLog(const char* message)
{
	Msg("%s", message);
}
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void __cdecl dummy(void){};
CEngineAPI::CEngineAPI()
{
	hGame = 0;
	hRender = 0;
	hTuner = 0;
	pCreate = 0;
	pDestroy = 0;
	tune_pause = dummy;
	tune_resume = dummy;
}

CEngineAPI::~CEngineAPI()
{
}
extern u32 renderer_value; // con cmd

void CEngineAPI::Initialize(void)
{
#ifndef DEDICATED_SERVER
	const bool native=KichR4_NativeSelect(Core.Params);
	// Native selection happens before any legacy DLL/device can be created.
	if (!native && !KichR4_GameInitialize(Core.Params, KichR4_WriteProbeLog, &kich_r4_level_files))
		KichR4_RunHardwareProbeIfRequested(Core.Params, KichR4_WriteProbeLog, &kich_r4_level_files);
#endif
	//////////////////////////////////////////////////////////////////////////
	// render
	LPCSTR r1_name = "xrRender_R1.dll";
	LPCSTR r2_name = "xrRender_R2.dll";

#ifndef DEDICATED_SERVER
	if (native){::Render=KichR4_CreateNativeRenderer();Msg("[R4 native B5] selected by -kich_r4_game; legacy renderer DLL loading bypassed");}
	if (!native && psDeviceFlags.test(rsR2))
	{
		// try to initialize R2
		Log("Loading DLL:", r2_name);
		hRender = LoadLibrary(r2_name);
		if (0 == hRender)
		{
			// try to load R1
			Msg("...Failed - incompatible hardware.");
		}
	}
#endif

	if (0 == hRender && !KichR4_NativeActive())
	{
		// try to load R1
		psDeviceFlags.set(rsR2, FALSE);
		renderer_value = 0; // con cmd

		Log("Loading DLL:", r1_name);
		hRender = LoadLibrary(r1_name);
		if (0 == hRender)
			R_CHK(GetLastError());
		R_ASSERT(hRender);
	}

	// game
	{
		LPCSTR g_name = "xrGame.dll";
		Log("Loading DLL:", g_name);
		hGame = LoadLibrary(g_name);
		if (0 == hGame)
			R_CHK(GetLastError());
		R_ASSERT2(hGame, "Game DLL raised exception during loading or there is no game DLL at all");
		pCreate = (Factory_Create*)GetProcAddress(hGame, "xrFactory_Create");
		R_ASSERT(pCreate);
		pDestroy = (Factory_Destroy*)GetProcAddress(hGame, "xrFactory_Destroy");
		R_ASSERT(pDestroy);
	}

	//////////////////////////////////////////////////////////////////////////
	// vTune
	tune_enabled = FALSE;
	if (strstr(Core.Params, "-tune"))
	{
		LPCSTR g_name = "vTuneAPI.dll";
		Log("Loading DLL:", g_name);
		hTuner = LoadLibrary(g_name);
		if (0 == hTuner)
			R_CHK(GetLastError());
		R_ASSERT2(hTuner, "Intel vTune is not installed");
		tune_enabled = TRUE;
		tune_pause = (VTPause*)GetProcAddress(hTuner, "VTPause");
		R_ASSERT(tune_pause);
		tune_resume = (VTResume*)GetProcAddress(hTuner, "VTResume");
		R_ASSERT(tune_resume);
	}
}

void CEngineAPI::Destroy(void)
{
	KichR4_GameShutdown();
	if (hGame)
	{
		FreeLibrary(hGame);
		hGame = 0;
	}
	if (hRender)
	{
		FreeLibrary(hRender);
		hRender = 0;
	}
	pCreate = 0;
	pDestroy = 0;
	Engine.Event._destroy();
	XRC.r_clear_compact();
}
