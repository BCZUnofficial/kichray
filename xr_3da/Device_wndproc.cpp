#include "stdafx.h"
#include "KichR4Native.h"

//-----------------------------------------------------------------------------
// Name: WndProc()
// Desc: Static msg handler which passes messages to the application class.
//-----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_ERASEBKGND:
		// DX11 owns the client image, including the last frame during loading.
		if (KichR4_NativeActive())
			return 1;
		break;
	case WM_PAINT:
		if (KichR4_NativeActive())
		{
			PAINTSTRUCT paint;
			BeginPaint(hWnd, &paint);
			EndPaint(hWnd, &paint);
			return 0;
		}
		break;
	case WM_ACTIVATE: {
		Device.OnWM_Activate(wParam, lParam);
		/*
					u16 fActive						= LOWORD(wParam);
					BOOL fMinimized					= (BOOL) HIWORD(wParam);
					BOOL bActive					= ((fActive!=WA_INACTIVE) && (!fMinimized))?TRUE:FALSE;

					if (bActive!=Device.bActive)
					{
						Device.bActive				= bActive;

						if (Device.b_is_Active)
						{
							Device.seqAppActivate.Process(rp_AppActivate);
		#ifndef		DEDICATED_SERVER
								ShowCursor			(FALSE);
		#endif
						}else
						{
							Device.seqAppDeactivate.Process(rp_AppDeactivate);
							ShowCursor				(TRUE);
						}
					}
		*/
	}

	break;
	case WM_SETCURSOR:
		if(KichR4_NativeActive()){if(Device.b_is_Active && LOWORD(lParam)==HTCLIENT){SetCursor(NULL);return 1;}return DefWindowProc(hWnd,uMsg,wParam,lParam);}
		return 1;
	case WM_SYSCOMMAND:
		// Prevent moving/sizing and power loss in fullscreen mode
		switch (wParam)
		{
		case SC_MOVE:
		case SC_SIZE:
		case SC_MAXIMIZE:
		case SC_MONITORPOWER:
			return 1;
			break;
		}
		break;
	case WM_CLOSE:
		return 0;
	case WM_KEYDOWN:
		break;
	default:
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
