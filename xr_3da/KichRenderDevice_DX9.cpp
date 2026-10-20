#include "stdafx.h"
#include "KichRenderDevice.h"
#include "KichR4Game.h"
#include "KichR4Capture.h"
#include "ResourceManager.h"
#include <string.h>

#pragma warning(disable : 4995)
#include <DXSDK/d3dx9.h>
#pragma warning(default : 4995)

namespace
{
class CKichRenderDeviceDX9 : public IKichRenderDevice
{
    KichRenderDeviceState last_state;
    uint32_t frames;
    uint32_t clears;
    uint32_t resets;
    bool first_present_logged;
    bool state_initialized;

    void ReadSize(KichRenderDeviceSize& size) const
    {
        size.width = HW.DevPP.BackBufferWidth;
        size.height = HW.DevPP.BackBufferHeight;
    }

  public:
    CKichRenderDeviceDX9() : last_state(KichDeviceOtherFailure), frames(0), clears(0), resets(0),
        first_present_logged(false), state_initialized(false) {}

    virtual void SetupGPU(bool software, bool non_pure, bool reference)
    {
        HW.Caps.bForceGPU_SW = software ? TRUE : FALSE;
        HW.Caps.bForceGPU_NonPure = non_pure ? TRUE : FALSE;
        HW.Caps.bForceGPU_REF = reference ? TRUE : FALSE;
    }

    virtual bool IsReferenceDevice() const { return HW.Caps.bForceGPU_REF != FALSE; }

    virtual void CreateHW(void* window, KichRenderDeviceSize& size)
    {
        frames = clears = resets = 0;
        first_present_logged = false;
        state_initialized = false;
        last_state = KichDeviceOtherFailure;
        HW.CreateDevice(static_cast<HWND>(window));
        ReadSize(size);
        Msg("[R4 bridge B1] backend=DX9 legacy (R1/R2); device interface active");
        Msg("[R4 bridge B1] create: %ux%u", size.width, size.height);
    }

    virtual void ResetHW(void* window, KichRenderDeviceSize& size)
    {
#ifndef _EDITOR
        KichR4_GameBeforeReset();
#endif
        HW.Reset(static_cast<HWND>(window));
        ReadSize(size);
        ++resets;
        Msg("[R4 bridge B1] reset hardware: %ux%u; count=%u", size.width, size.height, resets);
    }

    virtual void ValidateHW() { HW.Validate(); }

    virtual void DestroyHW()
    {
#ifndef _EDITOR
        KichR4_GameShutdown();
#endif
        Msg("[R4 bridge B1] destroy: frames=%u clears=%u resets=%u", frames, clears, resets);
        HW.DestroyDevice();
    }

    virtual void SetupStates()
    {
        HW.Caps.Update();
        for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
        {
            float fBias = -.5f;
            DWORD bias_bits;
            memcpy(&bias_bits, &fBias, sizeof(bias_bits));
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, 4));
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, bias_bits));
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
        }
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORVERTEX, TRUE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_ZENABLE, TRUE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_LOCALVIEWER, TRUE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE));
        if (psDeviceFlags.test(rsWireframe))
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME));
        else
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGCOLOR, 0));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE));
        if (HW.Caps.bTableFog)
        {
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR));
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
        }
        else
        {
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE));
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR));
        }
    }

    virtual void OnDeviceCreate() { RCache.OnDeviceCreate(); }
    virtual void OnDeviceDestroy() { RCache.OnDeviceDestroy(); }
    virtual void AllocateResources()
    {
        Device.Resources = xr_new<CResourceManager>();
        Msg("[R4 bridge B2] resource owner=DX9 legacy; lifecycle through backend adapter");
    }
    virtual void ReleaseResources() { xr_delete(Device.Resources); }
    virtual void OnResourcesCreate(const char* shader_database) { Device.Resources->OnDeviceCreate(shader_database); }
    virtual void OnResourcesDestroy(bool keep_textures) { Device.Resources->OnDeviceDestroy(keep_textures ? TRUE : FALSE); }
    virtual void BeginResourcesReset() { Device.Resources->reset_begin(); }
    virtual void EndResourcesReset() { Device.Resources->reset_end(); }
    virtual void DestroyNecessaryTextures() { Device.Resources->DestroyNecessaryTextures(); }

    virtual void TraceResetReferences(bool before)
    {
#ifdef DEBUG
        if (before)
            _SHOW_REF("*ref -CRenderDevice::ResetTotal: DeviceREF:", HW.pDevice);
        else
            _SHOW_REF("*ref +CRenderDevice::ResetTotal: DeviceREF:", HW.pDevice);
#else
        (void)before;
#endif
    }

    virtual KichRenderDeviceState GetDeviceState()
    {
        HW.Validate();
        const HRESULT hr = HW.pDevice->TestCooperativeLevel();
        KichRenderDeviceState state = KichDeviceReady;
        if (hr == D3DERR_DEVICELOST)
            state = KichDeviceLost;
        else if (hr == D3DERR_DEVICENOTRESET)
            state = KichDeviceNeedsReset;
        else if (FAILED(hr))
            state = KichDeviceOtherFailure;
        if (!state_initialized || state != last_state)
        {
            const char* name = state == KichDeviceReady ? "ready" :
                state == KichDeviceLost ? "lost" : state == KichDeviceNeedsReset ? "needs reset" : "other failure";
            Msg("[R4 bridge B1] state=%s; HRESULT=0x%08X", name, static_cast<uint32_t>(hr));
            last_state = state;
            state_initialized = true;
        }
        return state;
    }

    virtual void BeginFrame()
    {
        CHK_DX(HW.pDevice->BeginScene());
#ifndef _EDITOR
        KichR4_CaptureBeginFrame(Device.dwWidth, Device.dwHeight, Device.dwFrame);
#endif
        RCache.OnFrameBegin();
        RCache.set_CullMode(CULL_CW);
        RCache.set_CullMode(CULL_CCW);
        if (HW.Caps.SceneMode)
            OverdrawBegin();
    }

    virtual void Clear(bool clear_backbuffer)
    {
        CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_ZBUFFER | (clear_backbuffer ? D3DCLEAR_TARGET : 0) |
            (HW.Caps.bStencil ? D3DCLEAR_STENCIL : 0), D3DCOLOR_XRGB(0, 0, 0), 1, 0));
        ++clears;
    }

    virtual void ClearTarget()
    {
        CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1, 0));
    }

    virtual void PrepareFrameEnd(uint32_t width, uint32_t height)
    {
        VERIFY(HW.pDevice);
        if (HW.Caps.SceneMode)
            OverdrawEnd(width, height);
    }

    virtual void EndFrame()
    {
        RCache.OnFrameEnd();
        Memory.dbg_check();
        CHK_DX(HW.pDevice->EndScene());
        // One presenter per frame. R4 draws its own world after EndScene;
        // menus, diagnostics and an R4 failure use the already-rendered DX9 frame.
#ifndef _EDITOR
        if (KichR4_GamePresentPrepared())
            return;
#endif
        const HRESULT hr = HW.pDevice->Present(NULL, NULL, NULL, NULL);
        ++frames;
        if (hr == D3DERR_DEVICELOST)
            return; // The engine retries TestCooperativeLevel on the next Begin.
        // Preserve legacy handling of other Present results; do not add a fatal path in B1.
        if (!first_present_logged && SUCCEEDED(hr))
        {
            Msg("[R4 bridge B1] first frame presented through DX9 adapter");
            first_present_logged = true;
        }
    }

    virtual void OverdrawBegin()
    {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILENABLE, TRUE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILREF, 0));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILMASK, 0x00000000));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILWRITEMASK, 0xffffffff));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT));
        if (HW.Caps.SceneMode == 1)
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP));
        else
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_INCRSAT));
    }

    virtual void OverdrawEnd(uint32_t width, uint32_t height)
    {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILMASK, 0xff));
        CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 0, 0));
        RCache.OnFrameEnd();
        CHK_DX(HW.pDevice->SetFVF(FVF::F_TL));
        for (int i = 0; i < 12; i++)
        {
            u32 shade = i * 256 / 13;
            u32 color = D3DCOLOR_XRGB(shade, shade, shade);
            FVF::TL vertices[4];
            vertices[0].set(0.f, float(height), color, 0, 0);
            vertices[1].set(0.f, 0.f, color, 0, 0);
            vertices[2].set(float(width), float(height), color, 0, 0);
            vertices[3].set(float(width), 0.f, color, 0, 0);
            CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILREF, i));
            CHK_DX(HW.pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(FVF::TL)));
        }
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE));
    }

    virtual void SetCameraMatrices(const Fmatrix& view, const Fmatrix& projection,
        const Fmatrix& full, Fmatrix& inverse_full)
    {
        RCache.set_xform_view(view);
        RCache.set_xform_project(projection);
        D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&inverse_full), NULL,
            reinterpret_cast<const D3DXMATRIX*>(&full));
    }

    virtual void BeginFrameTiming(CStatTimer& real)
    {
        real.FrameStart();
        real.Begin();
    }

    virtual void EndFrameTiming(CStatTimer& real, CStatTimer& total)
    {
        real.End();
        real.FrameEnd();
        total.accum = real.accum;
    }
};
}

IKichRenderDevice& KichRenderDevice()
{
    static CKichRenderDeviceDX9 backend;
    return backend;
}
