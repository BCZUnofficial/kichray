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
    ID3DBlob* bytecode; /* Retained for a future B3/B4 input layout. */
    std::vector<KichR4ShaderBinding> bindings;
    std::vector<KichR4ShaderVariable> variables;
    Resource() : texture(NULL), buffer(NULL), srv(NULL), rtv(NULL), dsv(NULL),
        vs(NULL), ps(NULL), bytecode(NULL) { memset(&info,0,sizeof(info)); info.struct_size=sizeof(info); }
    ~Resource() {
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
    ObjectStore<Resource> resources;
    KichR4ResourceStats stats;
    Device() : runtime(NULL), compiler(NULL), hw(NULL), context(NULL), compile(NULL),
        reflect(NULL), thread(GetCurrentThreadId()), resources(ids) {
        memset(&stats,0,sizeof(stats)); stats.struct_size=sizeof(stats);
    }
    void ClearResources() {
        if (context) { context->ClearState(); context->Flush(); }
        stats.released += resources.Size();
        resources.Clear();
        stats.live_resources=0; stats.payload_bytes=0;
    }
    ~Device() {
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
