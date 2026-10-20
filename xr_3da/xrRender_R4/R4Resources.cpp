/* Native D3D11 ownership. No engine PCH, ResourceManager9 or D3DX9. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <string.h>
#include <vector>
#include <new>
#include "R4ResourceRules.h"
#include "R4FrameDX11.h"
#include "R4PipelineDX11.h"
#include "../KichR4Level.h"
#include "../KichR4Dds.h"

#if !defined(_M_IX86) && !defined(__i386__)
#error The R4 resource bridge is Win32/x86 only.
#endif

namespace {
using namespace kich_r4;
template<class T> void Release(T*& p) { if (p) { p->Release(); p = NULL; } }
template<class T> class Com {
    T* p_;
    Com(const Com&);
    Com& operator=(const Com&);
public:
    Com() : p_(NULL) {}
    ~Com() { Release(p_); }
    T* Get() const { return p_; }
    T** Out() { return &p_; } /* use only on an empty local Com. */
    T* operator->() const { return p_; }
};

HMODULE LoadSystemLibrary(const wchar_t* name)
{
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n) return NULL;
    const size_t tail = wcslen(name);
    if (n + tail + 2 > MAX_PATH) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return NULL; }
    path[n] = L'\\';
    memcpy(path+n+1, name, (tail+1)*sizeof(wchar_t));
    return LoadLibraryW(path);
}

typedef HRESULT (WINAPI *CreateDeviceFn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE,
    UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**,
    D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
typedef HRESULT (WINAPI *CompileFn)(const void*, SIZE_T, const char*,
    const D3D_SHADER_MACRO*, ID3DInclude*, const char*, const char*, UINT, UINT,
    ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *ReflectFn)(const void*, SIZE_T, REFIID, void**);

struct Resource {
    KichR4ResourceInfo info;
    ID3D11Texture2D* texture;
    ID3D11Buffer* buffer;
    ID3D11ShaderResourceView* srv;
    ID3D11RenderTargetView* rtv;
    ID3D11DepthStencilView* dsv;
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3DBlob* bytecode; /* Retained for B4 input layouts and reflection. */
    uint32_t buffer_stride;
    bool initialized;
    std::unique_ptr<PipelineDX11> pipeline;
    uint32_t* pipeline_released;
    std::vector<KichR4ShaderBinding> bindings;
    std::vector<KichR4ShaderVariable> variables;
    Resource() : texture(NULL), buffer(NULL), srv(NULL), rtv(NULL), dsv(NULL),
        vs(NULL), ps(NULL), bytecode(NULL),buffer_stride(0),initialized(false),pipeline_released(NULL)
        { memset(&info,0,sizeof(info)); info.struct_size=sizeof(info); }
    ~Resource() {
        if(pipeline_released)++*pipeline_released;
        Release(dsv); Release(rtv); Release(srv); Release(texture); Release(buffer);
        Release(vs); Release(ps); Release(bytecode);
    }
private:
    Resource(const Resource&);
    Resource& operator=(const Resource&);
};

/* No global STL/device destructor runs under DLL loader lock. Explicit API
 * destruction owns every device; DllMain in xrRender_R4.cpp stays empty. */
IdSource ids;
struct Device {
    HMODULE runtime, compiler;
    ID3D11Device* hw;
    ID3D11DeviceContext* context;
    CompileFn compile;
    ReflectFn reflect;
    DWORD thread;
    KichR4GeometryStats geometry_stats;
    ObjectStore<Resource> resources;
    KichR4ResourceStats stats;
    FrameDriverDX11 frame_driver;
    FrameController frame;
    Device() : runtime(NULL), compiler(NULL), hw(NULL), context(NULL), compile(NULL),
        reflect(NULL), thread(GetCurrentThreadId()), resources(ids),frame_driver(hw,context),frame(frame_driver) {
        memset(&stats,0,sizeof(stats)); stats.struct_size=sizeof(stats);
        memset(&geometry_stats,0,sizeof(geometry_stats));geometry_stats.struct_size=sizeof(geometry_stats);
    }
    void ClearResources() {
        if (context) { context->ClearState(); context->Flush(); }
        stats.released += resources.Size();
        resources.Clear();
        stats.live_resources=0; stats.payload_bytes=0;
    }
    ~Device() {
        frame.Close();
        /* Also close a partially created native target if an exception occurred
         * before FrameController could publish successful attachment. */
        frame_driver.Close();
        ClearResources();
        Release(context); Release(hw);
        if (compiler) FreeLibrary(compiler);
        if (runtime) FreeLibrary(runtime);
    }
    HRESULT Open() {
        runtime=LoadSystemLibrary(L"d3d11.dll");
        if (!runtime) return HRESULT_FROM_WIN32(GetLastError());
        CreateDeviceFn create=reinterpret_cast<CreateDeviceFn>(GetProcAddress(runtime,"D3D11CreateDevice"));
        if (!create) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL actual=static_cast<D3D_FEATURE_LEVEL>(0);
        HRESULT hr=create(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,requested,1,
            D3D11_SDK_VERSION,&hw,&actual,&context);
        if (FAILED(hr)) return hr;
        return hw && context && actual >= D3D_FEATURE_LEVEL_11_0 ? S_OK : E_UNEXPECTED;
    }
    HRESULT OpenCompiler() {
        if (compile && reflect) return S_OK;
        if (!compiler) compiler=LoadSystemLibrary(L"d3dcompiler_47.dll");
        if (!compiler) return HRESULT_FROM_WIN32(GetLastError());
        compile=reinterpret_cast<CompileFn>(GetProcAddress(compiler,"D3DCompile"));
        reflect=reinterpret_cast<ReflectFn>(GetProcAddress(compiler,"D3DReflect"));
        return compile && reflect ? S_OK : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    KichR4Handle Add(std::unique_ptr<Resource> r) {
        const uint32_t bytes=r->info.byte_size;
        KichR4Handle h=resources.Insert(std::move(r));
        ++stats.created; ++stats.live_resources; stats.payload_bytes+=bytes;
        return h;
    }
};
struct Slot { KichR4Handle id; Device* device; } slots[4] = {};
SRWLOCK api_lock = SRWLOCK_INIT;
struct APILock {
    APILock() { AcquireSRWLockExclusive(&api_lock); }
    ~APILock() { ReleaseSRWLockExclusive(&api_lock); }
};

Device* FindDevice(KichR4Handle id, int32_t& hr)
{
    for (size_t i=0;i<4;++i) {
        if (id && slots[i].id==id && slots[i].device) {
            if (slots[i].device->thread!=GetCurrentThreadId()) { hr=KICH_R4_E_THREAD; return NULL; }
            hr=KICH_R4_OK; return slots[i].device;
        }
    }
    hr=KICH_R4_E_HANDLE; return NULL;
}
template<class F> int32_t Protect(F action)
{
    /* The R4 project uses /EHa: native-call exceptions must unwind ownership
     * and this lock before a failure is returned across the C boundary. */
    APILock lock;
    try { return action(); }
    catch (const std::bad_alloc&) { return KICH_R4_E_MEMORY; }
    catch (...) { return static_cast<int32_t>(E_FAIL); }
}
template<class T> bool OutputOK(T* output, uint32_t bytes) { return output && bytes>=sizeof(T); }

int32_t KICH_R4_CALL CreateDevice(KichR4Handle* out)
{
    if (!out) return KICH_R4_E_ARGUMENT;
    *out=0;
    return Protect([&]() -> int32_t {
        size_t slot=0;
        for (;slot<4 && slots[slot].device;++slot) {}
        if (slot==4) return KICH_R4_E_MEMORY;
        std::unique_ptr<Device> d(new Device());
        HRESULT hr=d->Open();
        if (FAILED(hr)) return hr;
        const KichR4Handle id=ids.Take();
        slots[slot].id=id; slots[slot].device=d.release(); *out=id;
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL DestroyDevice(KichR4Handle id, KichR4ResourceStats* out, uint32_t bytes)
{
    if ((out && !OutputOK(out,bytes)) || (!out && bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(id,hr); if (!d) return hr;
        d->ClearResources();
        if (out) *out=d->stats;
        for (size_t i=0;i<4;++i) if (slots[i].device==d) { slots[i].device=NULL; slots[i].id=0; break; }
        delete d;
        return KICH_R4_OK;
    });
}

DXGI_FORMAT ColorFormat(uint32_t format)
{
    switch (format) {
    case KICH_R4_RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case KICH_R4_RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case KICH_R4_RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case KICH_R4_R32F: return DXGI_FORMAT_R32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
int32_t KICH_R4_CALL CreateTexture(KichR4Handle device, const KichR4TextureDesc* desc, KichR4Handle* out)
{
    if (!out) return KICH_R4_E_ARGUMENT;
    *out=0;
    if (!desc || !ValidTexture(*desc)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        std::unique_ptr<Resource> r(new Resource());
        const bool d24=desc->format==KICH_R4_DEPTH24_STENCIL8;
        const bool depth=d24 || desc->format==KICH_R4_DEPTH32F;
        const DXGI_FORMAT resource_format=depth ? (d24 ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_R32_TYPELESS) : ColorFormat(desc->format);
        const DXGI_FORMAT srv_format=depth ? (d24 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R32_FLOAT) : resource_format;
        const DXGI_FORMAT dsv_format=d24 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;
        D3D11_TEXTURE2D_DESC native={};
        native.Width=desc->width; native.Height=desc->height; native.MipLevels=1;
        native.ArraySize=1; native.Format=resource_format; native.SampleDesc.Count=1;
        native.Usage=D3D11_USAGE_DEFAULT;
        if (desc->bindings&KICH_R4_SHADER_READ) native.BindFlags|=D3D11_BIND_SHADER_RESOURCE;
        if (desc->bindings&KICH_R4_COLOR_TARGET) native.BindFlags|=D3D11_BIND_RENDER_TARGET;
        if (desc->bindings&KICH_R4_DEPTH_TARGET) native.BindFlags|=D3D11_BIND_DEPTH_STENCIL;
        hr=d->hw->CreateTexture2D(&native,NULL,&r->texture); if (FAILED(hr)) return hr;
        if (desc->bindings&KICH_R4_SHADER_READ) {
            D3D11_SHADER_RESOURCE_VIEW_DESC view={}; view.Format=srv_format;
            view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=1;
            hr=d->hw->CreateShaderResourceView(r->texture,&view,&r->srv); if (FAILED(hr)) return hr;
        }
        if (desc->bindings&KICH_R4_COLOR_TARGET) {
            hr=d->hw->CreateRenderTargetView(r->texture,NULL,&r->rtv); if (FAILED(hr)) return hr;
        }
        if (desc->bindings&KICH_R4_DEPTH_TARGET) {
            D3D11_DEPTH_STENCIL_VIEW_DESC view={}; view.Format=dsv_format;
            view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            hr=d->hw->CreateDepthStencilView(r->texture,&view,&r->dsv); if (FAILED(hr)) return hr;
        }
        r->info.kind=KICH_R4_TEXTURE_2D; r->info.format=desc->format;
        r->info.width=desc->width; r->info.height=desc->height; r->info.bindings=desc->bindings;
        r->info.byte_size=desc->width*desc->height*PixelBytes(desc->format);
        *out=d->Add(std::move(r)); return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL CreateBuffer(KichR4Handle device, const KichR4BufferDesc* desc,
    const void* data, uint32_t bytes, KichR4Handle* out)
{
    if (!out) return KICH_R4_E_ARGUMENT;
    *out=0;
    if (!desc || !ValidBuffer(*desc,data,bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        std::unique_ptr<Resource> r(new Resource());
        D3D11_BUFFER_DESC native={}; native.ByteWidth=desc->byte_size; native.Usage=D3D11_USAGE_DEFAULT;
        native.BindFlags=desc->kind==KICH_R4_VERTEX_BUFFER ? D3D11_BIND_VERTEX_BUFFER :
            desc->kind==KICH_R4_INDEX_BUFFER ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial={}; initial.pSysMem=data;
        hr=d->hw->CreateBuffer(&native,data ? &initial : NULL,&r->buffer); if (FAILED(hr)) return hr;
        r->info.kind=desc->kind; r->info.byte_size=desc->byte_size;
        r->buffer_stride=desc->stride;r->initialized=data!=NULL;
        *out=d->Add(std::move(r)); return KICH_R4_OK;
    });
}

bool CopyName(char* out, size_t capacity, const char* name)
{
    if (!name || strlen(name)>=capacity) return false;
    memcpy(out,name,strlen(name)+1); return true;
}
HRESULT ReadShaderTable(Device& d, Resource& r)
{
    Com<ID3D11ShaderReflection> reflection;
    /* SDK IID, stored locally so the DLL needs no dxguid import library. */
    const GUID reflection_iid={0x8d536ca1,0x0cca,0x4956,{0xa8,0x37,0x78,0x69,0x63,0x75,0x55,0x84}};
    HRESULT hr=d.reflect(r.bytecode->GetBufferPointer(),r.bytecode->GetBufferSize(),
        reflection_iid,reinterpret_cast<void**>(reflection.Out()));
    if (FAILED(hr)) return hr;
    D3D11_SHADER_DESC sd={}; hr=reflection->GetDesc(&sd); if (FAILED(hr)) return hr;
    for (UINT i=0;i<sd.BoundResources;++i) {
        D3D11_SHADER_INPUT_BIND_DESC bd={};
        hr=reflection->GetResourceBindingDesc(i,&bd); if (FAILED(hr)) return hr;
        KichR4ShaderBinding b={}; b.struct_size=sizeof(b); b.slot=bd.BindPoint; b.count=bd.BindCount;
        if (!CopyName(b.name,sizeof(b.name),bd.Name)) return E_NOTIMPL;
        if (bd.Type==D3D_SIT_CBUFFER) {
            b.kind=KICH_R4_BIND_CONSTANTS;
            ID3D11ShaderReflectionConstantBuffer* cb=reflection->GetConstantBufferByName(bd.Name);
            D3D11_SHADER_BUFFER_DESC cbd={}; hr=cb->GetDesc(&cbd); if (FAILED(hr)) return hr;
            b.byte_size=cbd.Size; b.first_variable=static_cast<uint32_t>(r.variables.size());
            b.variable_count=cbd.Variables;
            for (UINT v=0;v<cbd.Variables;++v) {
                ID3D11ShaderReflectionVariable* variable=cb->GetVariableByIndex(v);
                D3D11_SHADER_VARIABLE_DESC vd={}; hr=variable->GetDesc(&vd); if (FAILED(hr)) return hr;
                D3D11_SHADER_TYPE_DESC td={}; hr=variable->GetType()->GetDesc(&td); if (FAILED(hr)) return hr;
                KichR4ShaderVariable entry={}; entry.struct_size=sizeof(entry); entry.binding_index=i;
                entry.byte_offset=vd.StartOffset; entry.byte_size=vd.Size;
                entry.rows=td.Rows; entry.columns=td.Columns; entry.elements=td.Elements;
                if (!CopyName(entry.name,sizeof(entry.name),vd.Name)) return E_NOTIMPL;
                switch (td.Type) {
                case D3D_SVT_FLOAT: entry.scalar_type=KICH_R4_FLOAT; break;
                case D3D_SVT_INT: entry.scalar_type=KICH_R4_INT; break;
                case D3D_SVT_UINT: entry.scalar_type=KICH_R4_UINT; break;
                case D3D_SVT_BOOL: entry.scalar_type=KICH_R4_BOOL; break;
                default: return E_NOTIMPL;
                }
                switch (td.Class) {
                case D3D_SVC_SCALAR: case D3D_SVC_VECTOR: break;
                case D3D_SVC_MATRIX_ROWS: entry.matrix_layout=KICH_R4_MATRIX_ROWS; break;
                case D3D_SVC_MATRIX_COLUMNS: entry.matrix_layout=KICH_R4_MATRIX_COLUMNS; break;
                default: return E_NOTIMPL; /* no silent flattening of structures/interfaces. */
                }
                if (entry.byte_offset>b.byte_size || entry.byte_size>b.byte_size-entry.byte_offset) return E_UNEXPECTED;
                r.variables.push_back(entry);
            }
        } else if (bd.Type==D3D_SIT_TEXTURE && bd.Dimension==D3D_SRV_DIMENSION_TEXTURE2D)
            b.kind=KICH_R4_BIND_TEXTURE;
        else if (bd.Type==D3D_SIT_SAMPLER) b.kind=KICH_R4_BIND_SAMPLER;
        else return E_NOTIMPL; /* B2 v1 does not promise UAV/structured/cube resources. */
        r.bindings.push_back(b);
    }
    r.info.binding_count=static_cast<uint32_t>(r.bindings.size());
    r.info.variable_count=static_cast<uint32_t>(r.variables.size());
    return S_OK;
}
int32_t KICH_R4_CALL CompileShader(KichR4Handle device, const KichR4ShaderDesc* desc,
    const char* source, uint32_t length, KichR4Handle* out, char* errors, uint32_t error_bytes)
{
    if (!out) return KICH_R4_E_ARGUMENT;
    *out=0;
    if (errors && error_bytes) errors[0]=0;
    if (!desc || desc->struct_size!=sizeof(*desc) ||
        (desc->kind!=KICH_R4_VERTEX_SHADER && desc->kind!=KICH_R4_PIXEL_SHADER) ||
        !desc->entry[0] || !memchr(desc->entry,0,sizeof(desc->entry)) ||
        !memchr(desc->name,0,sizeof(desc->name)) || !source || !length || length>1024u*1024u ||
        (errors ? !error_bytes : error_bytes!=0)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        hr=d->OpenCompiler(); if (FAILED(hr)) return hr;
        std::unique_ptr<Resource> r(new Resource()); Com<ID3DBlob> messages;
        hr=d->compile(source,length,desc->name,NULL,NULL,desc->entry,
            desc->kind==KICH_R4_VERTEX_SHADER ? "vs_5_0" : "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,0,&r->bytecode,messages.Out());
        if (messages.Get() && errors && error_bytes) {
            const size_t n=messages->GetBufferSize()<error_bytes-1 ? messages->GetBufferSize() : error_bytes-1;
            memcpy(errors,messages->GetBufferPointer(),n); errors[n]=0;
        }
        if (FAILED(hr)) return hr;
        if (!r->bytecode) return E_UNEXPECTED;
        hr=ReadShaderTable(*d,*r); if (FAILED(hr)) return hr;
        if (desc->kind==KICH_R4_VERTEX_SHADER)
            hr=d->hw->CreateVertexShader(r->bytecode->GetBufferPointer(),r->bytecode->GetBufferSize(),NULL,&r->vs);
        else hr=d->hw->CreatePixelShader(r->bytecode->GetBufferPointer(),r->bytecode->GetBufferSize(),NULL,&r->ps);
        if (FAILED(hr)) return hr;
        r->info.kind=desc->kind;
        *out=d->Add(std::move(r)); return KICH_R4_OK;
    });
}

int32_t KICH_R4_CALL GetInfo(KichR4Handle device, KichR4Handle handle, KichR4ResourceInfo* out, uint32_t bytes)
{
    if (!OutputOK(out,bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        Resource* r=d->resources.Find(handle); if (!r) return KICH_R4_E_HANDLE;
        *out=r->info; return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL GetBinding(KichR4Handle device, KichR4Handle handle, uint32_t index,
    KichR4ShaderBinding* out, uint32_t bytes)
{
    if (!OutputOK(out,bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        Resource* r=d->resources.Find(handle); if (!r) return KICH_R4_E_HANDLE;
        if (index>=r->bindings.size()) return KICH_R4_E_ARGUMENT;
        *out=r->bindings[index]; return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL GetVariable(KichR4Handle device, KichR4Handle handle, uint32_t index,
    KichR4ShaderVariable* out, uint32_t bytes)
{
    if (!OutputOK(out,bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        Resource* r=d->resources.Find(handle); if (!r) return KICH_R4_E_HANDLE;
        if (index>=r->variables.size()) return KICH_R4_E_ARGUMENT;
        *out=r->variables[index]; return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL ReleaseResource(KichR4Handle device, KichR4Handle handle)
{
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        Resource* r=d->resources.Find(handle); if (!r) return KICH_R4_E_HANDLE;
        const uint32_t bytes=r->info.byte_size;
        d->resources.Erase(handle); --d->stats.live_resources; ++d->stats.released;
        d->stats.payload_bytes-=bytes; return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL GetStats(KichR4Handle device, KichR4ResourceStats* out, uint32_t bytes)
{
    if (!OutputOK(out,bytes)) return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr; Device* d=FindDevice(device,hr); if (!d) return hr;
        *out=d->stats; return KICH_R4_OK;
    });
}

int32_t KICH_R4_CALL CreateFrameTarget(KichR4Handle device,const KichR4FrameTargetDesc* desc)
{
    if(!desc)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);return d?d->frame.Create(*desc):hr;
    });
}
int32_t KICH_R4_CALL DestroyFrameTarget(KichR4Handle device)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->frame.Close();return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL ResizeFrame(KichR4Handle device,uint32_t w,uint32_t h)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);return d?d->frame.Resize(w,h):hr;
    });
}
int32_t KICH_R4_CALL BeginFrame(KichR4Handle device)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);return d?d->frame.Begin():hr;
    });
}
int32_t KICH_R4_CALL ClearFrame(KichR4Handle device,const KichR4FrameColor* color)
{
    if(!color)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);return d?d->frame.Clear(*color):hr;
    });
}
int32_t KICH_R4_CALL PresentFrame(KichR4Handle device,uint32_t interval)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);return d?d->frame.Present(interval):hr;
    });
}
int32_t KICH_R4_CALL GetFrameStats(KichR4Handle device,KichR4FrameStats* out,uint32_t bytes)
{
    if(!OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        *out=d->frame.Stats();return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL DestroyFrameDevice(KichR4Handle device,KichR4FrameStats* out,uint32_t bytes)
{
    if((out && !OutputOK(out,bytes)) || (!out && bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->frame.Close();if(out)*out=d->frame.Stats();
        for(size_t i=0;i<4;++i)if(slots[i].device==d){slots[i].device=NULL;slots[i].id=0;break;}
        delete d;return KICH_R4_OK;
    });
}

int32_t ShaderConstants(const Resource& shader,uint32_t& bytes,bool texture=false)
{
    bytes=0;
    for(size_t i=0;i<shader.bindings.size();++i) {
        const KichR4ShaderBinding& b=shader.bindings[i];
        if(texture && (b.kind==KICH_R4_BIND_TEXTURE || b.kind==KICH_R4_BIND_SAMPLER) && b.slot==0 && b.count==1)continue;
        if(b.kind!=KICH_R4_BIND_CONSTANTS || b.slot!=0 || b.count!=1 || !b.byte_size || b.byte_size>65536)
            return KICH_R4_E_UNSUPPORTED;
        if(b.byte_size>bytes)bytes=b.byte_size;
    }
    return KICH_R4_OK;
}
int32_t CreatePipelineImpl(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out,bool texture)
{
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;if(!desc || !ValidPipeline(*desc))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* vs=d->resources.Find(desc->vertex_shader);Resource* ps=d->resources.Find(desc->pixel_shader);
        if(!vs || !ps)return KICH_R4_E_HANDLE;
        if(vs->info.kind!=KICH_R4_VERTEX_SHADER || ps->info.kind!=KICH_R4_PIXEL_SHADER)return KICH_R4_E_ARGUMENT;
        uint32_t vs_bytes=0,ps_bytes=0;
        hr=ShaderConstants(*vs,vs_bytes);if(hr<0)return hr;
        hr=ShaderConstants(*ps,ps_bytes,texture);if(hr<0)return hr;
        std::unique_ptr<Resource> r(new Resource());r->pipeline.reset(new PipelineDX11());
        hr=r->pipeline->Create(d->hw,*desc,vs->vs,ps->ps,vs->bytecode,texture);if(hr<0)return hr;
        r->pipeline->constant_bytes=vs_bytes>ps_bytes?vs_bytes:ps_bytes;r->info.kind=KICH_R4_GRAPHICS_PIPELINE;
        Resource* saved=r.get();*out=d->Add(std::move(r));
        saved->pipeline_released=&d->geometry_stats.pipelines_released;++d->geometry_stats.pipelines_created;
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL CreatePipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,false);}
int32_t KICH_R4_CALL CreateTexturedPipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,true);}
int32_t KICH_R4_CALL UpdateBuffer(KichR4Handle device,KichR4Handle buffer,const void* data,uint32_t bytes)
{
    if(!data || !bytes)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* r=d->resources.Find(buffer);if(!r)return KICH_R4_E_HANDLE;
        if(!r->buffer || r->info.kind<KICH_R4_VERTEX_BUFFER || r->info.kind>KICH_R4_CONSTANT_BUFFER ||
            bytes!=r->info.byte_size)return KICH_R4_E_ARGUMENT;
        d->context->UpdateSubresource(r->buffer,0,NULL,data,0,0);r->initialized=true;
        ++d->geometry_stats.buffer_updates;return KICH_R4_OK;
    });
}
int32_t SubmitDraw(Device* d,const KichR4DrawIndexedDesc* desc,Resource* texture)
{
        if(!d->frame.CanDraw())return KICH_R4_E_STATE;
        Resource* p=d->resources.Find(desc->pipeline);Resource* vb=d->resources.Find(desc->vertex_buffer);
        Resource* ib=d->resources.Find(desc->index_buffer);Resource* cb=d->resources.Find(desc->constant_buffer);
        if(!p || !vb || !ib || !cb)return KICH_R4_E_HANDLE;
        if(!p->pipeline || p->pipeline->textured!=(texture!=NULL) || vb->info.kind!=KICH_R4_VERTEX_BUFFER || ib->info.kind!=KICH_R4_INDEX_BUFFER ||
            cb->info.kind!=KICH_R4_CONSTANT_BUFFER || vb->buffer_stride!=p->pipeline->stride ||
            cb->info.byte_size<p->pipeline->constant_bytes || !ValidDraw(*desc,ib->info.byte_size,ib->buffer_stride))return KICH_R4_E_ARGUMENT;
        if(!vb->initialized || !ib->initialized || !cb->initialized)return KICH_R4_E_STATE;
        if(desc->depth_mode==KICH_R4_DEPTH_ON) {const int32_t hr=d->frame_driver.EnsureDepth();if(hr<0)return hr;}
        const KichR4FrameStats size=d->frame.Stats();d->frame_driver.Bind(size.width,size.height);
        p->pipeline->Draw(d->context,*desc,vb->buffer,ib->buffer,cb->buffer,ib->buffer_stride,texture?texture->srv:NULL);
        ++d->geometry_stats.draw_calls;
        const uint64_t triangles=uint64_t(d->geometry_stats.triangles)+desc->index_count/3;
        d->geometry_stats.triangles=triangles>UINT32_MAX?UINT32_MAX:static_cast<uint32_t>(triangles);
        return KICH_R4_OK;
}
int32_t KICH_R4_CALL DrawIndexed(KichR4Handle device,const KichR4DrawIndexedDesc* desc)
{
    if(!desc || desc->struct_size!=sizeof(*desc))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        return SubmitDraw(d,desc,NULL);
    });
}
int32_t KICH_R4_CALL DrawTextured(KichR4Handle device,const KichR4TexturedDrawDesc* desc)
{
    if(!desc || desc->struct_size!=sizeof(*desc) || desc->draw.struct_size!=sizeof(desc->draw))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* texture=d->resources.Find(desc->texture);if(!texture)return KICH_R4_E_HANDLE;
        if(texture->info.kind!=KICH_R4_TEXTURE_2D || !texture->srv)return KICH_R4_E_ARGUMENT;
        return SubmitDraw(d,&desc->draw,texture);
    });
}
DXGI_FORMAT DdsFormat(uint32_t format)
{
    using namespace kich_r4_dds;
    switch(format){case RGBA:case RGBX:return DXGI_FORMAT_R8G8B8A8_UNORM;case BGRA:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case BGRX:return DXGI_FORMAT_B8G8R8X8_UNORM;case R8:return DXGI_FORMAT_R8_UNORM;
    case BC1:return DXGI_FORMAT_BC1_UNORM;case BC2:return DXGI_FORMAT_BC2_UNORM;case BC3:return DXGI_FORMAT_BC3_UNORM;
    case BC4:return DXGI_FORMAT_BC4_UNORM;case BC5:return DXGI_FORMAT_BC5_UNORM;case BC7:return DXGI_FORMAT_BC7_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;}
}
int32_t KICH_R4_CALL CreateDdsTexture(KichR4Handle device,const KichR4DdsDesc* desc,const void* data,uint32_t bytes,KichR4Handle* out)
{
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;
    if(!desc || desc->struct_size!=sizeof(*desc) || desc->flags)return KICH_R4_E_ARGUMENT;
    kich_r4_dds::Image image={};
    if(!kich_r4_dds::Parse(data,bytes,desc->max_dimension,image))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        std::unique_ptr<Resource> r(new Resource());
        D3D11_TEXTURE2D_DESC native={};native.Width=image.width;native.Height=image.height;native.MipLevels=image.count;
        native.ArraySize=1;native.Format=DdsFormat(image.format);native.SampleDesc.Count=1;
        native.Usage=D3D11_USAGE_IMMUTABLE;native.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial[15]={};
        for(uint32_t i=0;i<image.count;++i){initial[i].pSysMem=image.mips[i].data;initial[i].SysMemPitch=image.mips[i].row_pitch;initial[i].SysMemSlicePitch=image.mips[i].bytes;}
        /* Normalize legacy luminance/BGRA/XRGB to the mandatory RGBA8 format;
         * preserve grayscale and opaque X bits without a BGRA device flag. */
        std::vector<std::vector<unsigned char> > converted;
        if(image.format!=kich_r4_dds::RGBA && image.format<kich_r4_dds::BC1){native.Format=DXGI_FORMAT_R8G8B8A8_UNORM;converted.resize(image.count);
            const uint32_t source_pixel=image.format==kich_r4_dds::R8?1u:4u;
            for(uint32_t m=0;m<image.count;++m){const kich_r4_dds::Mip& mip=image.mips[m];std::vector<unsigned char>& rgba=converted[m];rgba.resize(size_t(mip.bytes/source_pixel)*4);
                for(uint32_t i=0;i<mip.bytes/source_pixel;++i)kich_r4_dds::ToRGBA(image.format,mip.data+i*source_pixel,rgba.data()+i*4);
                initial[m].pSysMem=rgba.data();initial[m].SysMemPitch=(mip.row_pitch/source_pixel)*4;initial[m].SysMemSlicePitch=static_cast<UINT>(rgba.size());}}
        hr=d->hw->CreateTexture2D(&native,initial,&r->texture);if(FAILED(hr))return hr;
        hr=d->hw->CreateShaderResourceView(r->texture,NULL,&r->srv);if(FAILED(hr))return hr;
        r->info.kind=KICH_R4_TEXTURE_2D;r->info.format=image.format>=kich_r4_dds::BC1?image.format:static_cast<uint32_t>(KICH_R4_RGBA8);r->info.width=image.width;r->info.height=image.height;
        r->info.byte_size=image.bytes*(image.format==kich_r4_dds::R8?4u:1u);r->info.bindings=KICH_R4_SHADER_READ;
        *out=d->Add(std::move(r));return KICH_R4_OK;
    });
}
KichR4GeometryStats GeometryStats(const Device& d)
{
    KichR4GeometryStats stats=d.geometry_stats;
    stats.depth_created=d.frame_driver.DepthCreated();stats.depth_released=d.frame_driver.DepthReleased();return stats;
}
int32_t KICH_R4_CALL GetGeometryStats(KichR4Handle device,KichR4GeometryStats* out,uint32_t bytes)
{
    if(!OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        *out=GeometryStats(*d);return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL DestroyGeometryDevice(KichR4Handle device,KichR4GeometryReport* out,uint32_t bytes)
{
    if((out && !OutputOK(out,bytes)) || (!out && bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->frame.Close();d->frame_driver.Close();d->ClearResources();
        KichR4GeometryReport report={};report.struct_size=sizeof(report);report.frame=d->frame.Stats();
        report.resources=d->stats;report.geometry=GeometryStats(*d);
        for(size_t i=0;i<4;++i)if(slots[i].device==d){slots[i].device=NULL;slots[i].id=0;break;}
        delete d;if(out)*out=report;return KICH_R4_OK;
    });
}
}

extern "C" int32_t KICH_R4_CALL KichR4_GetResourceAPI(uint32_t abi, KichR4ResourceAPI* out, uint32_t bytes)
{
    if (abi!=KICH_R4_RESOURCE_ABI || !OutputOK(out,bytes)) return KICH_R4_E_ARGUMENT;
    const KichR4ResourceAPI api={sizeof(KichR4ResourceAPI), KICH_R4_RESOURCE_ABI,
        sizeof(void*), 0, CreateDevice, DestroyDevice, CreateTexture, CreateBuffer,
        CompileShader, GetInfo, GetBinding, GetVariable, ReleaseResource, GetStats};
    *out=api;
    return KICH_R4_OK;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetFrameAPI(uint32_t abi,KichR4FrameAPI* out,uint32_t bytes)
{
    if(abi!=KICH_R4_FRAME_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4FrameAPI api={sizeof(api),KICH_R4_FRAME_ABI,sizeof(void*),0,
        CreateDevice,DestroyFrameDevice,CreateFrameTarget,DestroyFrameTarget,
        ResizeFrame,BeginFrame,ClearFrame,PresentFrame,GetFrameStats};
    *out=api;return KICH_R4_OK;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetGeometryAPI(uint32_t abi,KichR4GeometryAPI* out,uint32_t bytes)
{
    if(abi!=KICH_R4_GEOMETRY_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4GeometryAPI api={sizeof(api),KICH_R4_GEOMETRY_ABI,sizeof(void*),0,
        CreatePipeline,UpdateBuffer,DrawIndexed,GetGeometryStats,DestroyGeometryDevice};
    *out=api;return KICH_R4_OK;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetLevelAPI(uint32_t abi,KichR4LevelAPI* out,uint32_t bytes)
{
    if(abi!=KICH_R4_LEVEL_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4LevelAPI api={sizeof(api),KICH_R4_LEVEL_ABI,sizeof(void*),0,CreateDdsTexture,CreateTexturedPipeline,DrawTextured};
    *out=api;return KICH_R4_OK;
}
