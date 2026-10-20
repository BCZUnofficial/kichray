#include "stdafx.h"
#include "KichRenderDevice.h"

void CRenderDevice::overdrawBegin()
{
	KichRenderDevice().OverdrawBegin();
}

void CRenderDevice::overdrawEnd()
{
	KichRenderDevice().OverdrawEnd(dwWidth, dwHeight);
}
