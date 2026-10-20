#include "stdafx.h"

#include "xr_effgamma.h"
#include "render.h"
#include "KichRenderDevice.h"

void CRenderDevice::_SetupStates()
{
	// General Render States
	mView.identity();
	mProject.identity();
	mFullTransform.identity();
	vCameraPosition.set(0, 0, 0);
	vCameraDirection.set(0, 0, 1);
	vCameraTop.set(0, 1, 0);
	vCameraRight.set(1, 0, 0);

	KichRenderDevice().SetupStates();
}

void CRenderDevice::_Create(LPCSTR shName)
{
	Memory.mem_compact();

	// after creation
	b_is_Ready = TRUE;
	_SetupStates();

	// Signal everyone - device created
	KichRenderDevice().OnDeviceCreate();
	Gamma.Update();
	KichRenderDevice().OnResourcesCreate(shName);
	::Render->create();
	Statistic->OnDeviceCreate();

#ifndef DEDICATED_SERVER
	m_WireShader.create("editor\\wire");
	m_SelectionShader.create("editor\\selection");

	DU.OnDeviceCreate();
#endif

	dwFrame = 0;
}

void CRenderDevice::Create()
{
	if (b_is_Ready)
		return; // prevent double call
	Statistic = xr_new<CStats>();
	Log("Starting RENDER device...");

#ifdef _EDITOR
	psCurrentVidMode[0] = dwWidth;
	psCurrentVidMode[1] = dwHeight;
#endif

	KichRenderDeviceSize size;
	KichRenderDevice().CreateHW(m_hWnd, size);
	dwWidth = size.width;
	dwHeight = size.height;
	fWidth_2 = float(dwWidth / 2);
	fHeight_2 = float(dwHeight / 2);
	fFOV = 90.f;
	fASPECT = 1.f;

	string_path fname;
	FS.update_path(fname, "$game_data$", "shaders.xr");

	//////////////////////////////////////////////////////////////////////////
	KichRenderDevice().AllocateResources();
	_Create(fname);

	PreCache(0);
}
