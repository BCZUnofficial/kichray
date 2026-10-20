#ifndef KICH_R4_FRAME_DX11_H
#define KICH_R4_FRAME_DX11_H
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "R4FrameController.h"

namespace kich_r4 {
/* Native pointers are borrowed from the same B2 Device object inside R4. */
class FrameDriverDX11 : public FrameDriver {
    ID3D11Device*& device_;
    ID3D11DeviceContext*& context_;
    IDXGISwapChain* chain_;
    ID3D11RenderTargetView* view_;
    FrameDriverDX11(const FrameDriverDX11&);
    FrameDriverDX11& operator=(const FrameDriverDX11&);
public:
    FrameDriverDX11(ID3D11Device*& device,ID3D11DeviceContext*& context) :
        device_(device),context_(context),chain_(NULL),view_(NULL) {}
    virtual ~FrameDriverDX11() { Close(); }
    virtual int32_t Create(void*,uint32_t,uint32_t);
    virtual void DropView();
    virtual int32_t Resize(uint32_t,uint32_t);
    virtual int32_t MakeView();
    virtual void Bind(uint32_t,uint32_t);
    virtual void Clear(const float*);
    virtual int32_t Present(uint32_t,bool);
    virtual int32_t RemovedReason();
    virtual void Close();
};
}
#endif
