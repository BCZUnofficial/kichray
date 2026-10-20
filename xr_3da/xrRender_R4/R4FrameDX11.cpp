#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "R4FrameDX11.h"

namespace kich_r4 {
namespace {
template<class T>void ReleaseFrame(T*& p) { if(p){p->Release();p=NULL;} }
template<class T>struct LocalCOM {
    T* p;
    LocalCOM():p(NULL){}
    ~LocalCOM(){ReleaseFrame(p);}
};
}
int32_t FrameDriverDX11::Create(void* window,uint32_t w,uint32_t h)
{
    const HWND hwnd=static_cast<HWND>(window);
    DWORD process=0;
    if(!device_ || !context_ || !IsWindow(hwnd) ||
        GetWindowThreadProcessId(hwnd,&process)!=GetCurrentThreadId() || process!=GetCurrentProcessId())return E_INVALIDARG;
    if(chain_)return KICH_R4_E_STATE;
    LocalCOM<IDXGIDevice> dxgi;
    LocalCOM<IDXGIAdapter> adapter;
    LocalCOM<IDXGIFactory> factory;
    HRESULT hr=device_->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi.p));
    if(FAILED(hr))return hr;
    hr=dxgi.p->GetAdapter(&adapter.p);if(FAILED(hr))return hr;
    hr=adapter.p->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(&factory.p));if(FAILED(hr))return hr;
    DXGI_SWAP_CHAIN_DESC desc={};
    desc.BufferDesc.Width=w;desc.BufferDesc.Height=h;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.OutputWindow=hwnd;desc.Windowed=TRUE;
    /* Baseline DXGI blt model works with the B0 Win7+ runtime contract.
     * Borderless is owned by EXE; exclusive display mode is not changed. */
    desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    hr=factory.p->CreateSwapChain(device_,&desc,&chain_);if(FAILED(hr))return hr;
    hr=factory.p->MakeWindowAssociation(hwnd,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER);
    return hr;
}
void FrameDriverDX11::DropView()
{
    if(context_ && (view_ || chain_)) {
        context_->OMSetRenderTargets(0,NULL,NULL);
        context_->ClearState();context_->Flush();
    }
    ReleaseFrame(view_);
}
int32_t FrameDriverDX11::Resize(uint32_t w,uint32_t h)
{
    if(!chain_ || view_)return KICH_R4_E_STATE;
    return chain_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);
}
int32_t FrameDriverDX11::MakeView()
{
    if(!chain_ || view_)return KICH_R4_E_STATE;
    LocalCOM<ID3D11Texture2D> backbuffer;
    LocalCOM<ID3D11RenderTargetView> view;
    HRESULT hr=chain_->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer.p));
    if(FAILED(hr))return hr;
    hr=device_->CreateRenderTargetView(backbuffer.p,NULL,&view.p);
    if(SUCCEEDED(hr)) {view_=view.p;view.p=NULL;}
    /* No persistent GetBuffer reference survives this method. */
    return hr;
}
void FrameDriverDX11::Bind(uint32_t w,uint32_t h)
{
    context_->OMSetRenderTargets(1,&view_,NULL);
    D3D11_VIEWPORT viewport={};viewport.Width=static_cast<FLOAT>(w);viewport.Height=static_cast<FLOAT>(h);
    viewport.MinDepth=0.f;viewport.MaxDepth=1.f;context_->RSSetViewports(1,&viewport);
}
void FrameDriverDX11::Clear(const float* rgba) {context_->ClearRenderTargetView(view_,rgba);}
int32_t FrameDriverDX11::Present(uint32_t interval,bool test) {return chain_->Present(interval,test?DXGI_PRESENT_TEST:0);}
int32_t FrameDriverDX11::RemovedReason() {return device_?device_->GetDeviceRemovedReason():E_UNEXPECTED;}
void FrameDriverDX11::Close()
{
    DropView();
    /* Target is always windowed, including EXE-owned borderless. */
    ReleaseFrame(chain_);
}
}
