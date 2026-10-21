#pragma once
/* CPU visual/particle/detail implementations shared with the original engine.
 * Compiled into XR_3DA, with its own renderer and queues; no R1/R2 DLL. */
#include "stdafx.h"
#define KICH_R4_NATIVE_FRONTEND 1
#define R_R1 1
#define R_R2 2
#define RENDER R_R1
#include <DXSDK/d3dx9.h>
#include "ResourceManager.h"
#include "Render.h"
#include "IGame_Level.h"
#include "blenders/blender.h"
#include "blenders/blender_clsid.h"
#include "psystem.h"
#include "xrRender/xrRender_console.h"
#include "KichR4NativeRenderer.h"
#define TEX_POINT_ATT "internal\\internal_light_attpoint"
#define TEX_SPOT_ATT "internal\\internal_light_attclip"
