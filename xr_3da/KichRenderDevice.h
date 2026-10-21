#ifndef KICH_RENDER_DEVICE_H
#define KICH_RENDER_DEVICE_H

#include <stdint.h>

/* Engine math/timers are API-neutral; their definitions stay in xrCore. */
template <class T> struct _matrix;
typedef _matrix<float> Fmatrix;
class CStatTimer;

struct KichRenderDeviceSize
{
    uint32_t width;
    uint32_t height;
};

enum KichRenderDeviceState
{
    KichDeviceReady,
    KichDeviceLost,
    KichDeviceNeedsReset,
    KichDeviceOtherFailure
};

/*
 * B1 internal engine interface, NOT the C ABI exported by xrRender_R4.dll.
 * No D3D interfaces, formats or resources cross this boundary.
 * Native window is HWND on Win32; it is borrowed, never owned here.
 * Device/Render/ResourceManager class layouts are unchanged.
 * B2 resource lifetime hooks keep the legacy manager behind its DX9 adapter.
 */
class IKichRenderDevice
{
  public:
    virtual ~IKichRenderDevice() {}

    virtual void SetupGPU(bool software, bool non_pure, bool reference) = 0;
    virtual bool IsReferenceDevice() const = 0;
    virtual void CreateHW(void* window, KichRenderDeviceSize& size) = 0;
    virtual void ResetHW(void* window, KichRenderDeviceSize& size) = 0;
    virtual void ValidateHW() = 0;
    virtual void DestroyHW() = 0;
    virtual void SetupStates() = 0;
    virtual void OnDeviceCreate() = 0;
    virtual void OnDeviceDestroy() = 0;
    virtual void AllocateResources() = 0;
    virtual void ReleaseResources() = 0;
    virtual void OnResourcesCreate(const char* shader_database) = 0;
    virtual void OnResourcesDestroy(bool keep_textures) = 0;
    virtual void BeginResourcesReset() = 0;
    virtual void EndResourcesReset() = 0;
    virtual void DestroyNecessaryTextures() = 0;
    virtual void TraceResetReferences(bool before) = 0;

    virtual KichRenderDeviceState GetDeviceState() = 0;
    virtual void BeginFrame() = 0;
    virtual void Clear(bool clear_backbuffer) = 0;
    virtual void ClearTarget() = 0;
    /* Preserve overdraw-before-loading-UI, cache/end/present-after-UI order. */
    virtual void PrepareFrameEnd(uint32_t width, uint32_t height) = 0;
    virtual void EndFrame() = 0;
    virtual void OverdrawBegin() = 0;
    virtual void OverdrawEnd(uint32_t width, uint32_t height) = 0;
    virtual void SetCameraMatrices(const Fmatrix& view, const Fmatrix& projection,
        const Fmatrix& full, Fmatrix& inverse_full) = 0;
    /* Existing CPU render timers. These are not GPU timestamp queries. */
    virtual void BeginFrameTiming(CStatTimer& real) = 0;
    virtual void EndFrameTiming(CStatTimer& real, CStatTimer& total) = 0;
};

/* EXE-local singleton. Native R4 is fixed before device creation; otherwise DX9. */
IKichRenderDevice& KichRenderDevice();

#endif
