/* Native D3D11 ownership. No engine PCH, ResourceManager9 or D3DX9. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <new>
#include "R4ResourceRules.h"
#include "R4FrameDX11.h"
#include "R4PipelineDX11.h"
#include "R4DeferredRules.h"
#include "R4DetailRules.h"
#include "R4ModelRules.h"
#include "R4TelemetryRules.h"
#include <set>
#include <string>
#include "../KichR4NativeTools.h"
#include "../KichR4Detail.h"
#include "../RenderSnapshot.h"
#include <stdio.h>
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
    KichR4Handle handle;
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
    bool dynamic_instances,model_pose;
    std::vector<unsigned char> constant_contents;
    uint32_t instance_count,model_bones,model_skin_mode,model_capacity;
    char shader_entry[96],pixel_entry[96],shader_name[64];
    KichR4Handle vertex_shader,pixel_shader;
    KichR4PipelineDesc input_layout;
    uint64_t source_hash,bytecode_hash,created_frame,last_write_frame,last_write_pass,last_clear_frame,write_generation,emitted_epoch;
    uint32_t last_clear_flags,last_write_kind;float last_clear_color[4],last_clear_depth;
    float alpha[4];
    std::unique_ptr<PipelineDX11> pipeline;
    uint32_t* pipeline_released;
    std::vector<KichR4ShaderBinding> bindings;
    std::vector<KichR4ShaderVariable> variables;
    Resource() : texture(NULL), buffer(NULL), srv(NULL), rtv(NULL), dsv(NULL),
        vs(NULL), ps(NULL), bytecode(NULL),buffer_stride(0),initialized(false),dynamic_instances(false),model_pose(false),instance_count(0),model_bones(0),model_skin_mode(0),model_capacity(0),pipeline_released(NULL)
        { handle=0;vertex_shader=pixel_shader=source_hash=bytecode_hash=write_generation=emitted_epoch=last_write_pass=0;
          created_frame=last_write_frame=last_clear_frame=UINT64_MAX;last_clear_flags=last_write_kind=0;last_clear_depth=1;
          memset(&input_layout,0,sizeof(input_layout));memset(last_clear_color,0,sizeof(last_clear_color)); memset(&info,0,sizeof(info)); info.struct_size=sizeof(info);memset(shader_name,0,sizeof(shader_name));memset(shader_entry,0,sizeof(shader_entry));memset(pixel_entry,0,sizeof(pixel_entry));memset(alpha,0,sizeof(alpha)); }
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
    DrawStateDX11 draw_state;
    uint64_t unchanged_constant_updates=0;
    CompileFn compile;
    ReflectFn reflect;
    DWORD thread;
    KichR4GeometryStats geometry_stats;
    ObjectStore<Resource> resources;
    KichR4ResourceStats stats;
    FrameDriverDX11 frame_driver;
    FrameController frame;
    KichR4PassDesc pass;
    bool pass_active;
    KichR4SnapshotSink snapshot_sink;
    KichR4TelemetryContext telemetry;KichR4TelemetrySink telemetry_sink;
    uint64_t telemetry_epoch,pass_ordinal,draw_ordinal,created_total,released_total,uploads_total,uploaded_bytes_total;
    uint64_t resource_records,resource_records_dropped,route_observations,route_records,route_records_dropped,callback_failures;
    uint64_t backbuffer_write,backbuffer_generation;std::set<uint64_t> pass_routes;
    KichR4Handle bound_textures[DrawTextureSlots];
    uint32_t profile_failure;bool capture_query_requested;
    KichR4TelemetryTiming timing_results[8];uint32_t timing_count;
    struct ProfileSlot {ID3D11Query* disjoint;ID3D11Query* stamp[8];bool pending;
        KichR4TelemetryContext origin;uint32_t valid_mask,status;uint64_t poll_count;
        ProfileSlot():disjoint(NULL),pending(false),valid_mask(127),status(KICH_R4_TIMING_NONE),poll_count(0){memset(stamp,0,sizeof(stamp));memset(&origin,0,sizeof(origin));}
        void Reset(){Release(disjoint);for(unsigned i=0;i<8;++i)Release(stamp[i]);pending=false;memset(&origin,0,sizeof(origin));status=0;poll_count=0;}
        ~ProfileSlot(){Reset();}} profile[4];
    uint32_t profile_write;bool profile_open;
    Device() : runtime(NULL), compiler(NULL), hw(NULL), context(NULL), compile(NULL),
        reflect(NULL), thread(GetCurrentThreadId()), resources(ids),frame_driver(hw,context),frame(frame_driver),pass_active(false),snapshot_sink(NULL),profile_write(0),profile_open(false) {
        memset(bound_textures,0,sizeof(bound_textures));telemetry_sink=NULL;telemetry_epoch=pass_ordinal=draw_ordinal=created_total=released_total=uploads_total=uploaded_bytes_total=0;
        resource_records=resource_records_dropped=route_observations=route_records=route_records_dropped=callback_failures=0;
        backbuffer_write=UINT64_MAX;backbuffer_generation=0;profile_failure=0;capture_query_requested=false;timing_count=0;memset(timing_results,0,sizeof(timing_results));memset(&telemetry,0,sizeof(telemetry));
        memset(&pass,0,sizeof(pass));
        memset(&stats,0,sizeof(stats)); stats.struct_size=sizeof(stats);
        memset(&geometry_stats,0,sizeof(geometry_stats));geometry_stats.struct_size=sizeof(geometry_stats);
    }
    void ClearResources() {
        draw_state.Invalidate();
        pass_active=false;memset(bound_textures,0,sizeof(bound_textures));
        snapshot_sink=NULL;
        if (context) { context->ClearState(); context->Flush(); }
        released_total+=resources.Size();stats.released += resources.Size();
        resources.Clear();
        stats.live_resources=0; stats.payload_bytes=0;
    }
    ~Device() {
        frame.Close();
        /* Also close a partially created native target if an exception occurred
         * before FrameController could publish successful attachment. */
        frame_driver.Close();
        ClearResources();
        for(unsigned i=0;i<4;++i)profile[i].Reset();
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
    void Emit(const char* category,const char* id,const char* key,const char* value){
        if(!telemetry_sink || !(telemetry.flags&KICH_R4_TELEMETRY_DETAILED))return;
        KichR4TelemetryRecord record={};record.struct_size=sizeof(record);
        snprintf(record.category,sizeof(record.category),"%s",category);snprintf(record.id,sizeof(record.id),"%s",id);
        snprintf(record.key,sizeof(record.key),"%s",key);snprintf(record.value,sizeof(record.value),"%s",value?value:"NOT_MEASURED");
        try{telemetry_sink(&record);}catch(...){++callback_failures;}
    }
    void Number(const char* category,const char* id,const char* key,uint64_t value){
        // Timing-only sessions carry exact frame identity without serializing
        // detailed records. Gate before formatting, not only inside Emit.
        if(!telemetry_sink || !(telemetry.flags&KICH_R4_TELEMETRY_DETAILED))return;
        char text[32];snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(value));Emit(category,id,key,text);
    }
    void Write(Resource* r,uint32_t kind=2){if(!r)return;r->initialized=true;++r->write_generation;r->last_write_kind=kind;r->last_write_pass=kind==1?0:pass_ordinal;if(telemetry.struct_size)r->last_write_frame=telemetry.engine_frame;}
    void Upload(Resource* r,uint32_t bytes){++uploads_total;uploaded_bytes_total+=bytes;Write(r,1);}
    void Outputs(bool depth_write){for(unsigned i=0;i<pass.color_count;++i){if(pass.colors[i])Write(resources.Find(pass.colors[i]));else{backbuffer_write=telemetry.struct_size?telemetry.engine_frame:UINT64_MAX;++backbuffer_generation;}}if(depth_write && pass.depth)Write(resources.Find(pass.depth));}
    void Counters(){
        if(!telemetry_sink || !(telemetry.flags&KICH_R4_TELEMETRY_DETAILED))return;
        Number("BACKEND_COUNTERS","registry","created_total",created_total);Number("BACKEND_COUNTERS","registry","released_total",released_total);
        Number("BACKEND_COUNTERS","registry","live_resources",resources.Size());Number("BACKEND_COUNTERS","registry","payload_bytes",stats.payload_bytes);
        Number("BACKEND_COUNTERS","registry","buffer_uploads_total",uploads_total);Number("BACKEND_COUNTERS","registry","buffer_uploaded_bytes_total",uploaded_bytes_total);
        Number("BACKEND_COUNTERS","submission","draw_state_applications",draw_state.draws);Number("BACKEND_COUNTERS","submission","draw_state_api_calls",draw_state.state_calls);
        Number("BACKEND_COUNTERS","submission","unchanged_constant_updates",unchanged_constant_updates);
        Emit("BACKEND_COUNTERS","submission","counter_scope","DEVICE_LIFETIME; state_api_calls excludes pass/frame setup; unchanged CB calls perform no GPU upload");
        Number("BACKEND_COUNTERS","capture","resource_records",resource_records);Number("BACKEND_COUNTERS","capture","resource_records_dropped",resource_records_dropped);
        Number("BACKEND_COUNTERS","capture","binding_route_observations",route_observations);Number("BACKEND_COUNTERS","capture","binding_route_records",route_records);
        Number("BACKEND_COUNTERS","capture","binding_route_records_dropped",route_records_dropped);Number("BACKEND_COUNTERS","capture","callback_failures",callback_failures);
        Emit("BACKEND_COUNTERS","registry","payload_scope","REGISTERED_LOGICAL_GPU_PAYLOAD; NOT_RESIDENCY; EXCLUDES_COM_STATE_BYTECODE_SWAPCHAIN_AND_QUERIES");
        uint64_t queries=0;for(unsigned p=0;p<4;++p){if(profile[p].disjoint)++queries;for(unsigned i=0;i<8;++i)if(profile[p].stamp[i])++queries;}
        Number("BACKEND_COUNTERS","diagnostic","timestamp_query_objects",queries);Number("BACKEND_COUNTERS","diagnostic","pending_completion_records",timing_count);
        Emit("BACKEND_COUNTERS","diagnostic","query_gpu_bytes","NOT_MEASURED");Emit("BACKEND_COUNTERS","registry","deferred_driver_release_bytes","NOT_MEASURED; RELEASE_CALL_IS_NOT_GPU_RESIDENCY_EVIDENCE");
    }
    void TimingResult(const KichR4TelemetryContext& origin,uint32_t status,uint32_t mask,const double* ms=NULL){
        if(!origin.capture_id)return;
        if(timing_count>=8){++callback_failures;return;}
        KichR4TelemetryTiming& result=timing_results[timing_count++];memset(&result,0,sizeof(result));result.struct_size=sizeof(result);
        result.status=status;result.valid_mask=mask;result.capture_id=origin.capture_id;result.engine_frame=origin.engine_frame;result.render_frame=origin.render_frame;
        if(ms)memcpy(result.milliseconds,ms,sizeof(result.milliseconds));
    }
    KichR4Handle Add(std::unique_ptr<Resource> r) {
        const uint32_t bytes=r->info.byte_size;
        KichR4Handle h=resources.Insert(std::move(r));
        ++created_total;++stats.created; ++stats.live_resources; stats.payload_bytes+=bytes;
        Resource* added=resources.Find(h);if(added)added->handle=h;if(added && telemetry.struct_size){added->created_frame=telemetry.engine_frame;if(added->initialized){added->last_write_frame=telemetry.engine_frame;added->last_write_kind=4;++added->write_generation;}}
        if(telemetry_sink){char id[64];snprintf(id,sizeof(id),"resource_%llu",static_cast<unsigned long long>(h));Emit("RESOURCE_EVENT",id,"action","CREATED");}
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
        if(desc->kind==KICH_R4_CONSTANT_BUFFER){native.Usage=D3D11_USAGE_DYNAMIC;native.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            r->constant_contents.resize(desc->byte_size);if(data)memcpy(r->constant_contents.data(),data,bytes);}
        D3D11_SUBRESOURCE_DATA initial={}; initial.pSysMem=data;
        hr=d->hw->CreateBuffer(&native,data ? &initial : NULL,&r->buffer); if (FAILED(hr)) return hr;
        r->info.kind=desc->kind; r->info.byte_size=desc->byte_size;
        r->buffer_stride=desc->stride;r->initialized=data!=NULL;
        if(desc->kind==KICH_R4_CONSTANT_BUFFER && data && bytes==DeferredConstantsBytes)memcpy(r->alpha,static_cast<const unsigned char*>(data)+DeferredAlphaOffset,16);
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
        } else if (bd.Type==D3D_SIT_TEXTURE &&
            (bd.Dimension==D3D_SRV_DIMENSION_TEXTURE2D || bd.Dimension==D3D_SRV_DIMENSION_TEXTURECUBE))
            b.kind=KICH_R4_BIND_TEXTURE;
        else if (bd.Type==D3D_SIT_SAMPLER) b.kind=KICH_R4_BIND_SAMPLER;
        else return E_NOTIMPL; /* UAV/structured and other texture dimensions are unsupported. */
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
        r->info.kind=desc->kind;r->source_hash=TelemetryHash(source,length);r->bytecode_hash=TelemetryHash(r->bytecode->GetBufferPointer(),r->bytecode->GetBufferSize());
        CopyName(r->shader_entry,sizeof(r->shader_entry),desc->entry);CopyName(r->shader_name,sizeof(r->shader_name),desc->name);
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
        char id[64];snprintf(id,sizeof(id),"resource_%llu",static_cast<unsigned long long>(handle));d->Emit("RESOURCE_EVENT",id,"action","RELEASED");
        d->draw_state.Invalidate(); // Raw COM pointer addresses may be reused after release.
        ++d->released_total;d->resources.Erase(handle); --d->stats.live_resources; ++d->stats.released;
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
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;d->draw_state.Invalidate();return d->frame.Create(*desc);
    });
}
int32_t KICH_R4_CALL DestroyFrameTarget(KichR4Handle device)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->draw_state.Invalidate();d->pass_active=false;d->frame.Close();return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL ResizeFrame(KichR4Handle device,uint32_t w,uint32_t h)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->draw_state.Invalidate();d->pass_active=false;return d->frame.Resize(w,h);
    });
}
int32_t KICH_R4_CALL BeginFrame(KichR4Handle device)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->draw_state.Invalidate();d->pass_active=false;return d->frame.Begin();
    });
}
int32_t KICH_R4_CALL ClearFrame(KichR4Handle device,const KichR4FrameColor* color)
{
    if(!color)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;hr=d->frame.Clear(*color);
        if(hr==KICH_R4_OK){d->backbuffer_write=d->telemetry.struct_size?d->telemetry.engine_frame:UINT64_MAX;++d->backbuffer_generation;
            char value[128];snprintf(value,sizeof(value),"rgba=[%.9g,%.9g,%.9g,%.9g]",color->rgba[0],color->rgba[1],color->rgba[2],color->rgba[3]);d->Emit("GPU_BACKBUFFER","swapchain","clear",value);}
        return hr;
    });
}
int32_t KICH_R4_CALL PresentFrame(KichR4Handle device,uint32_t interval)
{
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        d->draw_state.Invalidate();d->pass_active=false;return d->frame.Present(interval);
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

int32_t ShaderConstants(const Resource& shader,uint32_t& bytes,bool texture=false,bool advanced=false,uint32_t model_capacity=0)
{
    bytes=0;bool palette=false;
    for(size_t i=0;i<shader.bindings.size();++i) {
        const KichR4ShaderBinding& b=shader.bindings[i];
        if(model_capacity && b.kind==KICH_R4_BIND_CONSTANTS && b.slot==1 && b.count==1 && b.byte_size==KichR4ModelPaletteBytes(model_capacity)){palette=true;continue;}
        if(advanced && b.count==1 && ((b.kind==KICH_R4_BIND_TEXTURE && b.slot<DrawTextureSlots) ||
            (b.kind==KICH_R4_BIND_SAMPLER && b.slot<DrawSamplerSlots)))continue;
        if(texture && (b.kind==KICH_R4_BIND_TEXTURE || b.kind==KICH_R4_BIND_SAMPLER) && b.slot==0 && b.count==1)continue;
        if(b.kind!=KICH_R4_BIND_CONSTANTS || b.slot!=0 || b.count!=1 || !b.byte_size || b.byte_size>65536)
            return KICH_R4_E_UNSUPPORTED;
        if(b.byte_size>bytes)bytes=b.byte_size;
    }
    return model_capacity && !palette?KICH_R4_E_UNSUPPORTED:KICH_R4_OK;
}
int32_t CreatePipelineImpl(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out,bool texture,bool stream=false,bool advanced=false,bool instanced=false,uint32_t model_capacity=0)
{
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;if(!desc || !ValidPipeline(*desc))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* vs=d->resources.Find(desc->vertex_shader);Resource* ps=d->resources.Find(desc->pixel_shader);
        if(!vs || !ps)return KICH_R4_E_HANDLE;
        if(vs->info.kind!=KICH_R4_VERTEX_SHADER || ps->info.kind!=KICH_R4_PIXEL_SHADER)return KICH_R4_E_ARGUMENT;
        uint32_t vs_bytes=0,ps_bytes=0;
        hr=ShaderConstants(*vs,vs_bytes,false,false,model_capacity);if(hr<0)return hr;
        hr=ShaderConstants(*ps,ps_bytes,texture,advanced);if(hr<0)return hr;
        std::unique_ptr<Resource> r(new Resource());r->pipeline.reset(new PipelineDX11());
        hr=r->pipeline->Create(d->hw,*desc,vs->vs,ps->ps,vs->bytecode,texture,stream,advanced,instanced,model_capacity!=0);if(hr<0)return hr;
        r->pipeline->constant_bytes=vs_bytes>ps_bytes?vs_bytes:ps_bytes;r->pipeline->model_capacity=model_capacity;r->info.kind=KICH_R4_GRAPHICS_PIPELINE;
        CopyName(r->shader_entry,sizeof(r->shader_entry),vs->shader_entry);CopyName(r->pixel_entry,sizeof(r->pixel_entry),ps->shader_entry);
        r->vertex_shader=desc->vertex_shader;r->pixel_shader=desc->pixel_shader;r->input_layout=*desc;
        Resource* saved=r.get();*out=d->Add(std::move(r));
        saved->pipeline_released=&d->geometry_stats.pipelines_released;++d->geometry_stats.pipelines_created;
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL CreatePipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,false);}
int32_t KICH_R4_CALL CreateTexturedPipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,true);}
int32_t KICH_R4_CALL CreateStreamPipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,true,true);}
int32_t KICH_R4_CALL CreateDeferredPipeline(KichR4Handle device,const KichR4PipelineDesc* desc,KichR4Handle* out)
{return CreatePipelineImpl(device,desc,out,true,true,true);}
int32_t KICH_R4_CALL CreateDetailPipeline(KichR4Handle device,const KichR4DetailPipelineDesc* desc,KichR4Handle* out){
    if(out)*out=0;
    if(!desc || desc->struct_size!=sizeof(*desc))return KICH_R4_E_ARGUMENT;
    KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=sizeof(KichR4DetailVertex);
    p.vertex_shader=desc->vs;p.pixel_shader=desc->ps;p.element_count=2;
    p.elements[0]={KICH_R4_POSITION,0,KICH_R4_FLOAT3,0};p.elements[1]={KICH_R4_TEXCOORD,0,KICH_R4_FLOAT4,12};
    return CreatePipelineImpl(device,&p,out,true,true,true,true);
}
int32_t KICH_R4_CALL CreateModelPipeline(KichR4Handle device,const KichR4ModelPipelineDesc* desc,KichR4Handle* out){
    if(out)*out=0;
    if(!desc || desc->struct_size!=sizeof(*desc) || !KichR4ModelValidCapacity(desc->bone_capacity))return KICH_R4_E_ARGUMENT;
    KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=sizeof(KichR4ModelVertex);
    p.vertex_shader=desc->vs;p.pixel_shader=desc->ps;p.element_count=2;
    p.elements[0]={KICH_R4_POSITION,0,KICH_R4_FLOAT3,0};p.elements[1]={KICH_R4_TEXCOORD,0,KICH_R4_FLOAT2,12};
    return CreatePipelineImpl(device,&p,out,true,true,true,false,desc->bone_capacity);
}
int32_t KICH_R4_CALL CreateModelPose(KichR4Handle device,uint32_t capacity,KichR4Handle* out){
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;if(!KichR4ModelValidCapacity(capacity))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        std::unique_ptr<Resource> r(new Resource());D3D11_BUFFER_DESC b={};b.ByteWidth=KichR4ModelPaletteBytes(capacity);
        b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        hr=d->hw->CreateBuffer(&b,NULL,&r->buffer);if(hr<0)return hr;
        r->info.kind=KICH_R4_CONSTANT_BUFFER;r->info.byte_size=b.ByteWidth;r->model_pose=true;r->model_capacity=capacity;*out=d->Add(std::move(r));return KICH_R4_OK;});
}
int32_t KICH_R4_CALL UploadModelPose(KichR4Handle device,KichR4Handle handle,const KichR4ModelPose* pose){
    if(!pose || !ValidModelPose(*pose))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* r=d->resources.Find(handle);if(!r)return KICH_R4_E_HANDLE;
        if(!r->model_pose || !KichR4ModelValidCapacity(r->model_capacity) || pose->bone_count>r->model_capacity ||
            r->info.byte_size!=KichR4ModelPaletteBytes(r->model_capacity))return KICH_R4_E_ARGUMENT;
        D3D11_MAPPED_SUBRESOURCE mapped={};hr=d->context->Map(r->buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(hr<0)return hr;
        const uint32_t supplied=16+pose->bone_count*64;
        memcpy(mapped.pData,pose,supplied);
        memset(static_cast<unsigned char*>(mapped.pData)+supplied,0,r->info.byte_size-supplied);
        d->context->Unmap(r->buffer,0);r->initialized=true;
        r->model_bones=pose->bone_count;r->model_skin_mode=pose->skin_mode;d->Upload(r,r->info.byte_size);++d->geometry_stats.buffer_updates;return KICH_R4_OK;});
}
int32_t KICH_R4_CALL CreateDetailInstances(KichR4Handle device,uint32_t capacity,KichR4Handle* out){
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;if(!ValidDetailCapacity(capacity))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        std::unique_ptr<Resource> r(new Resource());D3D11_BUFFER_DESC b={};
        b.ByteWidth=capacity*sizeof(KichR4DetailInstance);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        hr=d->hw->CreateBuffer(&b,NULL,&r->buffer);if(hr<0)return hr;
        r->info.kind=KICH_R4_VERTEX_BUFFER;r->info.byte_size=b.ByteWidth;r->buffer_stride=sizeof(KichR4DetailInstance);
        r->dynamic_instances=true;*out=d->Add(std::move(r));return KICH_R4_OK;});
}
int32_t KICH_R4_CALL UploadDetailInstances(KichR4Handle device,KichR4Handle handle,const KichR4DetailInstance* data,uint32_t count){
    if(!data || !ValidDetailCapacity(count))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* r=d->resources.Find(handle);if(!r)return KICH_R4_E_HANDLE;
        if(!r->dynamic_instances || uint64_t(count)*sizeof(*data)>r->info.byte_size)return KICH_R4_E_ARGUMENT;
        D3D11_MAPPED_SUBRESOURCE mapped={};hr=d->context->Map(r->buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(hr<0)return hr;
        memcpy(mapped.pData,data,size_t(count)*sizeof(*data));d->context->Unmap(r->buffer,0);
        r->initialized=true;r->instance_count=count;d->Upload(r,count*sizeof(*data));++d->geometry_stats.buffer_updates;return KICH_R4_OK;});
}
int32_t KICH_R4_CALL UpdateBuffer(KichR4Handle device,KichR4Handle buffer,const void* data,uint32_t bytes)
{
    if(!data || !bytes)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* r=d->resources.Find(buffer);if(!r)return KICH_R4_E_HANDLE;
        if(!r->buffer || r->dynamic_instances || r->model_pose || r->info.kind<KICH_R4_VERTEX_BUFFER || r->info.kind>KICH_R4_CONSTANT_BUFFER ||
            bytes!=r->info.byte_size)return KICH_R4_E_ARGUMENT;
        if(r->info.kind==KICH_R4_CONSTANT_BUFFER){
            if(r->constant_contents.size()!=bytes)return KICH_R4_E_STATE;
            if(r->initialized && !memcmp(r->constant_contents.data(),data,bytes)){++d->unchanged_constant_updates;return KICH_R4_OK;}
            D3D11_MAPPED_SUBRESOURCE mapped={};hr=d->context->Map(r->buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
            memcpy(mapped.pData,data,bytes);d->context->Unmap(r->buffer,0);
            memcpy(r->constant_contents.data(),data,bytes);
        }else d->context->UpdateSubresource(r->buffer,0,NULL,data,0,0);
        r->initialized=true;
        if(r->info.kind==KICH_R4_CONSTANT_BUFFER && bytes==DeferredConstantsBytes)memcpy(r->alpha,static_cast<const unsigned char*>(data)+DeferredAlphaOffset,16);
        d->Upload(r,bytes);++d->geometry_stats.buffer_updates;return KICH_R4_OK;
    });
}

void ResourceIdentity(char* id,size_t bytes,KichR4Handle handle){snprintf(id,bytes,"resource_%llu",static_cast<unsigned long long>(handle));}
void SnapshotResource(Device* d,KichR4Handle handle){
    if(!d->telemetry_sink || !(d->telemetry.flags&KICH_R4_TELEMETRY_DETAILED) || !handle)return;
    Resource* r=d->resources.Find(handle);if(!r || r->emitted_epoch==d->telemetry_epoch)return;
    r->emitted_epoch=d->telemetry_epoch;
    if(d->resource_records>=1024){++d->resource_records_dropped;return;}++d->resource_records;
    char id[96],value[384];ResourceIdentity(id,sizeof(id),handle);
    d->Number("GPU_RESOURCE",id,"handle",handle);d->Number("GPU_RESOURCE",id,"kind",r->info.kind);
    d->Number("GPU_RESOURCE",id,"payload_bytes",r->info.byte_size);d->Number("GPU_RESOURCE",id,"device_generation",d->telemetry.device_generation);
    d->Number("GPU_RESOURCE",id,"level_generation",d->telemetry.level_generation);d->Emit("GPU_RESOURCE",id,"owner","R4_DEVICE_REGISTRY");
    d->Emit("GPU_RESOURCE",id,"storage_role",r->model_pose?"MODEL_PALETTE_BUFFER":r->dynamic_instances?"DETAIL_INSTANCE_BUFFER":r->pipeline?"GRAPHICS_PIPELINE_CACHE":r->bytecode?"SHADER_BYTECODE_REFLECTION_CACHE":r->rtv || r->dsv?"SCENE_RENDER_TARGET":r->texture?"TEXTURE_CACHE_OR_DYNAMIC_UPLOAD":r->info.kind==KICH_R4_CONSTANT_BUFFER?"PASS_CONSTANT_BUFFER":"GEOMETRY_BUFFER");
    d->Emit("GPU_RESOURCE",id,"semantic_owner","SEE_SOURCE_RESOURCE_DICTIONARY; NOT_INFERRED_FROM_HANDLE");
    if(r->created_frame==UINT64_MAX)d->Emit("GPU_RESOURCE",id,"created_engine_frame","PRE_INSTRUMENTATION");else d->Number("GPU_RESOURCE",id,"created_engine_frame",r->created_frame);
    d->Emit("GPU_RESOURCE",id,"initialization",r->initialized?"WRITTEN_OR_INITIAL_DATA":"UNINITIALIZED");
    if(r->texture){D3D11_TEXTURE2D_DESC t={};r->texture->GetDesc(&t);
        snprintf(value,sizeof(value),"width=%u;height=%u;mips=%u;array_size=%u;samples=%u;quality=%u;dxgi_format=%u;usage=%u;bind_flags=%u;cpu_access=%u;misc=%u",
            t.Width,t.Height,t.MipLevels,t.ArraySize,t.SampleDesc.Count,t.SampleDesc.Quality,t.Format,t.Usage,t.BindFlags,t.CPUAccessFlags,t.MiscFlags);
        d->Emit("GPU_RESOURCE",id,"texture_description",value);
        const bool srgb=t.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || t.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || t.Format==DXGI_FORMAT_BC1_UNORM_SRGB || t.Format==DXGI_FORMAT_BC2_UNORM_SRGB || t.Format==DXGI_FORMAT_BC3_UNORM_SRGB || t.Format==DXGI_FORMAT_BC7_UNORM_SRGB;
        d->Emit("GPU_RESOURCE",id,"format_color_space",srgb?"SRGB_VIEW_DECODE":"NO_SRGB_VIEW_DECODE; AUTHORING_COLOR_SPACE_UNKNOWN");
        if(r->srv){D3D11_SHADER_RESOURCE_VIEW_DESC v={};r->srv->GetDesc(&v);snprintf(value,sizeof(value),"dxgi_format=%u;dimension=%u;first_mip=0;mip_count=%u;first_slice=0;slice_count=%u",v.Format,v.ViewDimension,t.MipLevels,t.ArraySize);d->Emit("GPU_RESOURCE",id,"srv_subresources",value);}
        if(r->rtv){D3D11_RENDER_TARGET_VIEW_DESC v={};r->rtv->GetDesc(&v);snprintf(value,sizeof(value),"dxgi_format=%u;dimension=%u;first_mip=0;mip_count=1;first_slice=0;slice_count=1",v.Format,v.ViewDimension);d->Emit("GPU_RESOURCE",id,"rtv_subresources",value);}
        if(r->dsv){D3D11_DEPTH_STENCIL_VIEW_DESC v={};r->dsv->GetDesc(&v);snprintf(value,sizeof(value),"dxgi_format=%u;dimension=%u;first_mip=0;mip_count=1;first_slice=0;slice_count=1;flags=%u",v.Format,v.ViewDimension,v.Flags);d->Emit("GPU_RESOURCE",id,"dsv_subresources",value);}
    }
    if(r->buffer){D3D11_BUFFER_DESC b={};r->buffer->GetDesc(&b);snprintf(value,sizeof(value),"bytes=%u;stride=%u;usage=%u;bind_flags=%u;cpu_access=%u",b.ByteWidth,r->buffer_stride,b.Usage,b.BindFlags,b.CPUAccessFlags);d->Emit("GPU_RESOURCE",id,"buffer_description",value);}
    if(r->bytecode){
        d->Emit("SHADER",id,"source_name",r->shader_name);d->Emit("SHADER",id,"compile_flags","D3DCOMPILE_ENABLE_STRICTNESS");d->Emit("SHADER",id,"entry",r->shader_entry);d->Emit("SHADER",id,"target",r->vs?"vs_5_0":"ps_5_0");
        d->Emit("SHADER",id,"defines","NONE; COMPILED_SOURCE_ALREADY_EXPANDED");d->Emit("SHADER",id,"hash_algorithm","FNV1A_64_CONTENT_ID_NOT_CRYPTOGRAPHIC");
        snprintf(value,sizeof(value),"%016llx",static_cast<unsigned long long>(r->source_hash));d->Emit("SHADER",id,"source_hash",value);
        snprintf(value,sizeof(value),"%016llx",static_cast<unsigned long long>(r->bytecode_hash));d->Emit("SHADER",id,"bytecode_hash",value);
        d->Emit("SHADER",id,"reflection_semantics","COMPILED_RESOURCE_DECLARATIONS; NOT_PROOF_OF_EXECUTED_PIXEL_BRANCH");
        for(size_t i=0;i<r->bindings.size();++i){const KichR4ShaderBinding& b=r->bindings[i];char binding_id[96];snprintf(binding_id,sizeof(binding_id),"shader_%llu_binding_%u",static_cast<unsigned long long>(handle),static_cast<unsigned>(i));
            snprintf(value,sizeof(value),"name=%s;kind=%u;slot=%u;count=%u;bytes=%u;stage=%s",b.name,b.kind,b.slot,b.count,b.byte_size,r->vs?"VS":"PS");d->Emit("SHADER_DECLARATION",binding_id,"reflection",value);}
        for(size_t i=0;i<r->variables.size();++i){const KichR4ShaderVariable& v=r->variables[i];char variable_id[96];snprintf(variable_id,sizeof(variable_id),"shader_%llu_variable_%u",static_cast<unsigned long long>(handle),static_cast<unsigned>(i));
            snprintf(value,sizeof(value),"name=%s;binding_index=%u;offset=%u;bytes=%u;scalar_type=%u;rows=%u;columns=%u;elements=%u;matrix_layout=%u",v.name,v.binding_index,v.byte_offset,v.byte_size,v.scalar_type,v.rows,v.columns,v.elements,v.matrix_layout);d->Emit("SHADER_VARIABLE",variable_id,"reflection",value);}
    }
    if(r->pipeline){
        d->Number("PIPELINE",id,"vertex_shader_handle",r->vertex_shader);d->Number("PIPELINE",id,"pixel_shader_handle",r->pixel_shader);
        d->Number("PIPELINE",id,"vertex_stride",r->input_layout.vertex_stride);d->Emit("PIPELINE",id,"position_normal_space",r->pipeline->model?"GPU_MODEL_SKINNING; SEE_MODEL_METADATA":r->pipeline->instanced?"DETAIL_INSTANCE_TRANSFORM":"SEE_SOURCE_GEOMETRY_METADATA");
        for(uint32_t i=0;i<r->input_layout.element_count;++i){const KichR4VertexElement& e=r->input_layout.elements[i];char key[32];snprintf(key,sizeof(key),"input_element_%u",i);snprintf(value,sizeof(value),"semantic=%u;index=%u;format=%u;offset=%u",e.semantic,e.semantic_index,e.format,e.byte_offset);d->Emit("PIPELINE",id,key,value);}
        d->Emit("PIPELINE",id,"additional_input_layout",r->pipeline->model?"slot0:BLENDWEIGHT0_R32_FLOAT@20;BLENDINDICES0_R16G16_UINT@24;NORMAL0_R32_UINT@28;TANGENT0_R32_UINT@32;BINORMAL0_R32_UINT@36;TEXCOORD1_FLOAT4@40;COLOR0_UNORM8X4@56;TEXCOORD2_UINT@60":r->pipeline->instanced?"slot1:TEXCOORD1..4_R32G32B32A32_FLOAT@0,16,32,48;step_rate=1":"NONE");
        SnapshotResource(d,r->vertex_shader);SnapshotResource(d,r->pixel_shader);
    }
}
void ResourceHistory(Device* d,const char* category,const char* id,KichR4Handle handle){
    Resource* r=d->resources.Find(handle);if(!r)return;
    d->Emit(category,id,"last_writer_kind",r->last_write_kind==1?"CPU_UPLOAD":r->last_write_kind==2?"SUBMITTED_DRAW; NOT_PIXEL_COVERAGE_PROOF":r->last_write_kind==3?"TARGET_CLEAR":r->last_write_kind==4?"INITIAL_DATA":"UNKNOWN_OR_UNWRITTEN");d->Emit(category,id,"producer_status",r->initialized?"INITIAL_DATA_OR_WRITE_OBSERVED":"NO_PRODUCER_OBSERVED");d->Number(category,id,"write_generation",r->write_generation);d->Number(category,id,"last_writer_pass",r->last_write_pass);
    if(r->last_write_frame==UINT64_MAX){d->Emit(category,id,"last_writer_engine_frame","UNKNOWN_OR_UNWRITTEN");d->Emit(category,id,"age_frames","UNKNOWN");}
    else{d->Number(category,id,"last_writer_engine_frame",r->last_write_frame);if(d->telemetry.engine_frame>=r->last_write_frame)d->Number(category,id,"age_frames",d->telemetry.engine_frame-r->last_write_frame);else d->Emit(category,id,"age_frames","ENGINE_FRAME_RESTARTED");}
    if(r->last_clear_frame==UINT64_MAX)d->Emit(category,id,"last_clear_engine_frame","NEVER_OBSERVED");else{d->Number(category,id,"last_clear_engine_frame",r->last_clear_frame);char value[192];snprintf(value,sizeof(value),"flags=%u;color=[%.9g,%.9g,%.9g,%.9g];depth=%.9g",r->last_clear_flags,r->last_clear_color[0],r->last_clear_color[1],r->last_clear_color[2],r->last_clear_color[3],r->last_clear_depth);d->Emit(category,id,"last_clear",value);}
}
void SnapshotPass(Device* d){
    if(!d->telemetry_sink)return;
    char id[96],value[384];snprintf(id,sizeof(id),"pass_%llu",static_cast<unsigned long long>(d->pass_ordinal));
    d->Number("GPU_PASS",id,"engine_frame",d->telemetry.engine_frame);d->Number("GPU_PASS",id,"render_frame",d->telemetry.render_frame);
    d->Number("GPU_PASS",id,"execution_ordinal",d->pass_ordinal);
    snprintf(value,sizeof(value),"x=0;y=0;width=%u;height=%u;depth_min=0;depth_max=1",d->pass.width,d->pass.height);d->Emit("GPU_PASS",id,"viewport",value);
    snprintf(value,sizeof(value),"flags=%u;color=[%.9g,%.9g,%.9g,%.9g];depth=%.9g;stencil=%s",d->pass.clear_flags,d->pass.color[0],d->pass.color[1],d->pass.color[2],d->pass.color[3],d->pass.depth_value,d->pass.clear_flags&KICH_R4_CLEAR_STENCIL?"ZERO":"NOT_CLEARED");d->Emit("GPU_PASS",id,"clear",value);
    for(uint32_t i=0;i<d->pass.color_count;++i){char key[32];snprintf(key,sizeof(key),"rtv_%u",i);d->Number("GPU_PASS",id,key,d->pass.colors[i]);SnapshotResource(d,d->pass.colors[i]);}
    d->Number("GPU_PASS",id,"dsv",d->pass.depth);SnapshotResource(d,d->pass.depth);
    d->Emit("GPU_PASS",id,"output_view_range","MIP_0_SLICE_0; HANDLE_0_RTV_IS_BACKBUFFER; HANDLE_0_DSV_IS_UNBOUND");
    d->Emit("GPU_PASS",id,"input_transition","PS_SRV_SLOTS_0_TO_9_UNBOUND_BEFORE_OM_TARGETS");
}
void TelemetryDraw(Device* d,const KichR4DrawIndexedDesc& desc,Resource* pipeline,const KichR4StreamDrawDesc* state,const KichR4Handle* textures,const KichR4DetailDrawDesc* detail,KichR4Handle pose){
    ++d->draw_ordinal;
    try { if(d->telemetry_sink && (d->telemetry.flags&KICH_R4_TELEMETRY_DETAILED)){
        ++d->route_observations;uint64_t route=TelemetryHash(&desc.pipeline,sizeof(desc.pipeline));
        if(textures)route^=TelemetryHash(textures,DrawTextureSlots*sizeof(KichR4Handle));
        route^=TelemetryHash(&desc.constant_buffer,sizeof(desc.constant_buffer));route^=pose;
        const uint32_t states[]={desc.cull_mode,desc.depth_mode,state?state->depth_write:desc.depth_mode,state?state->blend_enable:0,state?state->source_blend:0,state?state->dest_blend:0,state?state->clamp_texture:0,state?state->scissor_enable:0};route^=TelemetryHash(states,sizeof(states));
        const bool fresh=d->pass_routes.find(route)==d->pass_routes.end();
        if(fresh && d->pass_routes.size()>=16)++d->route_records_dropped;
        else if(fresh){d->pass_routes.insert(route);++d->route_records;
            char id[96],value[384];snprintf(id,sizeof(id),"pass_%llu_route_%llu",static_cast<unsigned long long>(d->pass_ordinal),static_cast<unsigned long long>(d->draw_ordinal));
            d->Number("GPU_BINDING",id,"pass_ordinal",d->pass_ordinal);d->Number("GPU_BINDING",id,"first_draw_ordinal",d->draw_ordinal);d->Number("GPU_BINDING",id,"pipeline",desc.pipeline);
            if(d->pass_active){for(unsigned color=0;color<d->pass.color_count;++color){char attachment[96];snprintf(attachment,sizeof(attachment),"pass_%llu_rtv_%u",static_cast<unsigned long long>(d->pass_ordinal),color);ResourceHistory(d,"GPU_ATTACHMENT",attachment,d->pass.colors[color]);}if(d->pass.depth){char attachment[96];snprintf(attachment,sizeof(attachment),"pass_%llu_dsv",static_cast<unsigned long long>(d->pass_ordinal));ResourceHistory(d,"GPU_ATTACHMENT",attachment,d->pass.depth);}}
            d->Number("GPU_BINDING",id,"vs_b0",desc.constant_buffer);d->Number("GPU_BINDING",id,"ps_b0",desc.constant_buffer);d->Number("GPU_BINDING",id,"vs_b1",pose);
            d->Emit("GPU_BINDING",id,"scope","ACTUAL_SUBMITTED_BINDINGS; DECLARATIONS_AND_BRANCH_EXECUTION_ARE_DISTINCT");
            SnapshotResource(d,desc.pipeline);SnapshotResource(d,desc.vertex_buffer);SnapshotResource(d,desc.index_buffer);SnapshotResource(d,desc.constant_buffer);SnapshotResource(d,pose);if(detail)SnapshotResource(d,detail->instance_buffer);
            Resource* ps=d->resources.Find(pipeline->pixel_shader);
            for(unsigned slot=0;slot<DrawTextureSlots;++slot){KichR4Handle handle=textures?textures[slot]:0;char slot_id[96];snprintf(slot_id,sizeof(slot_id),"draw_%llu_ps_t%u",static_cast<unsigned long long>(d->draw_ordinal),slot);
                bool declared=false;if(ps)for(const auto& b:ps->bindings)if(b.kind==KICH_R4_BIND_TEXTURE && slot>=b.slot && slot-b.slot<b.count)declared=true;
                d->Number("GPU_TEXTURE_BINDING",slot_id,"pass_ordinal",d->pass_ordinal);d->Number("GPU_TEXTURE_BINDING",slot_id,"handle",handle);d->Emit("GPU_TEXTURE_BINDING",slot_id,"bound",handle?"YES":"NO");
                d->Emit("GPU_TEXTURE_BINDING",slot_id,"declared_in_bytecode",ps?(declared?"YES":"NO"):"SHADER_HANDLE_RELEASED");d->Emit("GPU_TEXTURE_BINDING",slot_id,"pixel_branch_executed","NOT_MEASURED");
                if(handle){SnapshotResource(d,handle);ResourceHistory(d,"GPU_TEXTURE_BINDING",slot_id,handle);d->Emit("GPU_TEXTURE_BINDING",slot_id,"rt_srv_conflict",d->pass_active?"NO_OVERLAPPING_SUBRESOURCE; PROVIDER_REJECTS_OVERLAP_BEFORE_DRAW":"BACKBUFFER_OUTPUT; REGISTERED_TEXTURE_CANNOT_ALIAS_BACKBUFFER");}
            }
            snprintf(value,sizeof(value),"filter=MIN_MAG_MIP_LINEAR;U=%s;V=%s;W=%s;comparison=NEVER;lod_min=0;lod_max=FLT_MAX",state && state->clamp_texture?"CLAMP":"WRAP",state && state->clamp_texture?"CLAMP":"WRAP",state && state->clamp_texture?"CLAMP":"WRAP");d->Emit("GPU_BINDING",id,"ps_s0",pipeline->pipeline->textured?value:"UNCHANGED_OR_NULL_BY_THIS_PIPELINE; UNUSED_BY_VALIDATED_SHADER");
            d->Emit("GPU_BINDING",id,"ps_s1",pipeline->pipeline->advanced?"COMPARISON_MIN_MAG_MIP_POINT;UVW=BORDER;BORDER=1,1,1,1;COMPARE=LESS_EQUAL;LOD=0..FLT_MAX":"UNCHANGED_OR_NULL_BY_THIS_PIPELINE; UNUSED_BY_VALIDATED_SHADER");
        }
    }
    }catch(...){++d->callback_failures;}
    if(d->pass_active)d->Outputs(desc.depth_mode && (!state || state->depth_write));else{d->backbuffer_write=d->telemetry.struct_size?d->telemetry.engine_frame:UINT64_MAX;++d->backbuffer_generation;}
}
void SnapshotDraw(Device* d,const KichR4DrawIndexedDesc& desc,Resource* pipeline,Resource* vb,Resource* cb,const KichR4StreamDrawDesc* state,const KichR4Handle* textures,const KichR4DetailDrawDesc* detail=NULL,KichR4Handle pose=0){
    TelemetryDraw(d,desc,pipeline,state,textures,detail,pose);
    if(!d->snapshot_sink)return;
    RenderSnapshotDraw event={};event.struct_size=sizeof(event);event.submitted=1;event.topology=4;
    event.index_count=desc.index_count;event.vertex_capacity=vb->info.byte_size/vb->buffer_stride;
    event.instance_count=detail?detail->instance_count:1;event.start_index=desc.start_index;event.first_instance=detail?detail->first_instance:0;
    event.pipeline=desc.pipeline;event.vs=pipeline->vertex_shader;event.ps=pipeline->pixel_shader;event.vb=desc.vertex_buffer;event.ib=desc.index_buffer;event.cb=desc.constant_buffer;event.instances=detail?detail->instance_buffer:0;
    memcpy(event.vertex_shader,pipeline->shader_entry,sizeof(event.vertex_shader));memcpy(event.pixel_shader,pipeline->pixel_entry,sizeof(event.pixel_shader));
    if(textures)memcpy(event.textures,textures,sizeof(event.textures));
    if(detail)strcpy(event.skin,"NONE_INSTANCED_DETAIL");
    const Resource* vs=d->resources.Find(pipeline->vertex_shader);const Resource* ps=d->resources.Find(pipeline->pixel_shader);
    const KichR4SurfaceState effective=state && state->surface.valid?state->surface:KichR4DefaultSurface();
    snprintf(event.states,sizeof(event.states),"vs_bytecode_hash=%016llx;ps_bytecode_hash=%016llx;model_capacity=%u;enum=D3D11;gpu_pass_ordinal=%llu;gpu_draw_ordinal=%llu;depth=%u;depth_write=%u;depth_func=%u;alpha_shader_func=%.0f;alpha_ref=%.8f;blend=%u;src_legacy=%u;dst_legacy=%u;blend_op=%u;color_write=%u;cull=%u;stencil=%u/%u/%u/%u/%u;scissor=%u;viewport_depth=%.5f/%.5f;raw_topology=TRIANGLELIST",
        static_cast<unsigned long long>(vs?vs->bytecode_hash:0),static_cast<unsigned long long>(ps?ps->bytecode_hash:0),pipeline->pipeline->model_capacity,static_cast<unsigned long long>(d->pass_active?d->pass_ordinal:0),static_cast<unsigned long long>(d->draw_ordinal),desc.depth_mode,state?state->depth_write:desc.depth_mode,effective.depth_func,cb->alpha[1],cb->alpha[0],state?state->blend_enable:0,state?state->source_blend:2,state?state->dest_blend:1,effective.blend_op,effective.color_mask,desc.cull_mode==KICH_R4_CULL_BACK?3:desc.cull_mode==KICH_R4_CULL_FRONT?2:1,effective.stencil,effective.stencil_func,effective.stencil_ref,effective.stencil_read,effective.stencil_write,state?state->scissor_enable:0,state?state->min_depth:0,state?state->max_depth:1);
    size_t used=0;for(unsigned i=0;i<(pipeline->pipeline->advanced?8u:1u);++i){KichR4SamplerState t=effective.samplers[i];
        if(!t.u)t=KichR4ReadSampler([](uint32_t,uint32_t fallback){return fallback;});
        if(state && state->clamp_texture)t.u=t.v=t.w=3;
        const int count=snprintf(event.samplers+used,sizeof(event.samplers)-used,"s%u:UVW=%u/%u/%u;min_mag_mip=%u/%u/%u;aniso=%u;border=%08x;bias_bits=%08x;maxmip=%u;",i?i+1:0,t.u,t.v,t.w,t.min_filter,t.mag_filter,t.mip_filter,t.anisotropy,t.border,t.lod_bias_bits,t.max_mip);
        if(count<0 || size_t(count)>=sizeof(event.samplers)-used)break;
        used+=size_t(count);}
    if(pipeline->pipeline->advanced && used<sizeof(event.samplers))snprintf(event.samplers+used,sizeof(event.samplers)-used,"s1:COMPARISON_POINT;UVW=BORDER;cmp=LESS_EQUAL");
    /* Callback cannot reenter GPU services; the EXE recorder catches its own
     * allocation failures and copies everything before returning. */
    d->snapshot_sink(&event);
}
int32_t SubmitDraw(Device* d,const KichR4DrawIndexedDesc* desc,Resource* texture,const KichR4StreamDrawDesc* extra=NULL)
{
        if(!d->frame.CanDraw())return KICH_R4_E_STATE;
        Resource* p=d->resources.Find(desc->pipeline);Resource* vb=d->resources.Find(desc->vertex_buffer);
        Resource* ib=d->resources.Find(desc->index_buffer);Resource* cb=d->resources.Find(desc->constant_buffer);
        if(!p || !vb || !ib || !cb)return KICH_R4_E_HANDLE;
        if(!p->pipeline || p->pipeline->advanced || p->pipeline->textured!=(texture!=NULL) || p->pipeline->stream!=(extra!=NULL) || vb->info.kind!=KICH_R4_VERTEX_BUFFER || ib->info.kind!=KICH_R4_INDEX_BUFFER ||
            cb->info.kind!=KICH_R4_CONSTANT_BUFFER || vb->buffer_stride!=p->pipeline->stride ||
            cb->info.byte_size<p->pipeline->constant_bytes || !ValidDraw(*desc,ib->info.byte_size,ib->buffer_stride))return KICH_R4_E_ARGUMENT;
        if(!vb->initialized || !ib->initialized || !cb->initialized)return KICH_R4_E_STATE;
        if(desc->depth_mode==KICH_R4_DEPTH_ON) {const int32_t hr=d->frame_driver.EnsureDepth();if(hr<0)return hr;}
        const KichR4FrameStats size=d->frame.Stats();d->frame_driver.Bind(size.width,size.height);
        D3D11_VIEWPORT viewport={};viewport.Width=static_cast<float>(size.width);viewport.Height=static_cast<float>(size.height);viewport.MaxDepth=1;
        d->draw_state.BeginPass(viewport); // Bind changed OM and viewport outside the deferred path.
        const int32_t drawn=p->pipeline->Draw(d->context,d->draw_state,*desc,vb->buffer,ib->buffer,cb->buffer,ib->buffer_stride,texture?texture->srv:NULL,extra);
        if(drawn<0)return drawn;
        d->pass_active=false;memset(d->bound_textures,0,sizeof(d->bound_textures));d->bound_textures[0]=texture?texture->handle:0;
        SnapshotDraw(d,*desc,p,vb,cb,extra,d->bound_textures);
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
int32_t KICH_R4_CALL DrawStream(KichR4Handle device,const KichR4StreamDrawDesc* desc)
{
    if(!desc || !ValidStreamDraw(*desc))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* texture=d->resources.Find(desc->texture);if(!texture)return KICH_R4_E_HANDLE;
        if(texture->info.kind!=KICH_R4_TEXTURE_2D || !texture->srv)return KICH_R4_E_ARGUMENT;
        return SubmitDraw(d,&desc->draw,texture,desc);
    });
}
int32_t KICH_R4_CALL BeginDeferredPass(KichR4Handle device,const KichR4PassDesc* pass)
{
    if(!pass || !ValidPass(*pass))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        if(!d->frame.CanDraw())return KICH_R4_E_STATE;
        ID3D11RenderTargetView* colors[4]={};ID3D11DepthStencilView* depth=NULL;
        for(uint32_t i=0;i<pass->color_count;++i){
            if(!pass->colors[i]){const KichR4FrameStats size=d->frame.Stats();
                if(size.width!=pass->width || size.height!=pass->height)return KICH_R4_E_ARGUMENT;
                colors[i]=d->frame_driver.ColorView();if(!colors[i])return KICH_R4_E_STATE;
            }else {Resource* r=d->resources.Find(pass->colors[i]);if(!r)return KICH_R4_E_HANDLE;
                if(!r->rtv || r->info.width!=pass->width || r->info.height!=pass->height)return KICH_R4_E_ARGUMENT;
                colors[i]=r->rtv;}
        }
        if(pass->depth){Resource* r=d->resources.Find(pass->depth);if(!r)return KICH_R4_E_HANDLE;
            if(!r->dsv || r->info.width!=pass->width || r->info.height!=pass->height)return KICH_R4_E_ARGUMENT;
            if((pass->clear_flags&KICH_R4_CLEAR_STENCIL) && r->info.format!=KICH_R4_DEPTH24_STENCIL8)return KICH_R4_E_ARGUMENT;
            depth=r->dsv;}
        d->draw_state.Invalidate(); // OM target changes may implicitly unbind SRV aliases.
        ID3D11ShaderResourceView* empty[DrawTextureSlots]={};d->context->PSSetShaderResources(0,DrawTextureSlots,empty);memset(d->bound_textures,0,sizeof(d->bound_textures));
        d->context->OMSetRenderTargets(pass->color_count,colors,depth);
        D3D11_VIEWPORT viewport={};viewport.Width=static_cast<float>(pass->width);viewport.Height=static_cast<float>(pass->height);viewport.MaxDepth=1;
        d->context->RSSetViewports(1,&viewport);
        d->draw_state.BeginPass(viewport);
        if(pass->clear_flags&KICH_R4_CLEAR_COLOR)for(uint32_t i=0;i<pass->color_count;++i)d->context->ClearRenderTargetView(colors[i],pass->color);
        if(pass->clear_flags&(KICH_R4_CLEAR_DEPTH|KICH_R4_CLEAR_STENCIL))d->context->ClearDepthStencilView(depth,
            ((pass->clear_flags&KICH_R4_CLEAR_DEPTH)?D3D11_CLEAR_DEPTH:0)|((pass->clear_flags&KICH_R4_CLEAR_STENCIL)?D3D11_CLEAR_STENCIL:0),pass->depth_value,0);
        d->pass=*pass;d->pass_active=true;++d->pass_ordinal;d->pass_routes.clear();
        for(uint32_t i=0;i<pass->color_count;++i)if(pass->clear_flags&KICH_R4_CLEAR_COLOR){Resource* r=d->resources.Find(pass->colors[i]);if(r){d->Write(r,3);r->last_clear_frame=d->telemetry.struct_size?d->telemetry.engine_frame:UINT64_MAX;r->last_clear_flags=KICH_R4_CLEAR_COLOR;memcpy(r->last_clear_color,pass->color,sizeof(r->last_clear_color));}else{d->backbuffer_write=d->telemetry.struct_size?d->telemetry.engine_frame:UINT64_MAX;++d->backbuffer_generation;}}
        if(pass->clear_flags&KICH_R4_CLEAR_DEPTH){Resource* r=d->resources.Find(pass->depth);d->Write(r,3);if(r){r->last_clear_frame=d->telemetry.struct_size?d->telemetry.engine_frame:UINT64_MAX;r->last_clear_flags=KICH_R4_CLEAR_DEPTH;r->last_clear_depth=pass->depth_value;}}
        SnapshotPass(d);return KICH_R4_OK;
    });
}
int32_t DrawDeferredOrDetail(KichR4Handle device,const KichR4DeferredDrawDesc* desc,const KichR4DetailDrawDesc* detail,const KichR4ModelDrawDesc* model=NULL)
{
    if(!desc || !ValidDeferredDraw(*desc))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        if(!d->frame.CanDraw() || !d->pass_active)return KICH_R4_E_STATE;
        const KichR4DrawIndexedDesc& draw=desc->state.draw;
        Resource* p=d->resources.Find(draw.pipeline);Resource* vb=d->resources.Find(draw.vertex_buffer);
        Resource* ib=d->resources.Find(draw.index_buffer);Resource* cb=d->resources.Find(draw.constant_buffer);
        if(!p || !vb || !ib || !cb)return KICH_R4_E_HANDLE;
        if(!p->pipeline || !p->pipeline->advanced || p->pipeline->instanced!=(detail!=NULL) || p->pipeline->model!=(model!=NULL) || vb->info.kind!=KICH_R4_VERTEX_BUFFER ||
            ib->info.kind!=KICH_R4_INDEX_BUFFER || cb->info.kind!=KICH_R4_CONSTANT_BUFFER ||
            vb->buffer_stride!=p->pipeline->stride || cb->info.byte_size<p->pipeline->constant_bytes ||
            !ValidDraw(draw,ib->info.byte_size,ib->buffer_stride) || (draw.depth_mode && !d->pass.depth))return KICH_R4_E_ARGUMENT;
        if(!vb->initialized || !ib->initialized || !cb->initialized)return KICH_R4_E_STATE;
        Resource* pose=NULL;
        if(model){pose=d->resources.Find(model->pose_buffer);if(!pose)return KICH_R4_E_HANDLE;
            if(!pose->model_pose || pose->model_capacity!=p->pipeline->model_capacity || pose->info.byte_size!=KichR4ModelPaletteBytes(p->pipeline->model_capacity))return KICH_R4_E_ARGUMENT;
            if(!pose->initialized)return KICH_R4_E_STATE;}
        Resource* instances=NULL;
        if(detail){instances=d->resources.Find(detail->instance_buffer);if(!instances)return KICH_R4_E_HANDLE;
            if(!instances->dynamic_instances || !ValidDetailRange(detail->first_instance,detail->instance_count,instances->instance_count))return KICH_R4_E_ARGUMENT;
            if(!instances->initialized)return KICH_R4_E_STATE;}
        ID3D11ShaderResourceView* inputs[DrawTextureSlots]={};KichR4Handle handles[DrawTextureSlots]={};
        memcpy(handles,desc->textures,sizeof(desc->textures));
        if(model){KichR4SurfaceState sampler_state=KichR4DefaultSurface();
            for(unsigned i=0;i<2;++i){handles[8+i]=model->material_textures[i];sampler_state.samplers[i]=model->material_samplers[i];}
            if(!KichR4ValidSurface(sampler_state))return KICH_R4_E_ARGUMENT;}
        for(unsigned i=0;i<DrawTextureSlots;++i)if(handles[i]){
            const KichR4Handle handle=handles[i];
            if(handle==d->pass.depth)return KICH_R4_E_ARGUMENT;
            for(unsigned c=0;c<d->pass.color_count;++c)if(handle==d->pass.colors[c])return KICH_R4_E_ARGUMENT;
            Resource* r=d->resources.Find(handle);if(!r)return KICH_R4_E_HANDLE;
            if(!r->srv)return KICH_R4_E_ARGUMENT;
            inputs[i]=r->srv;
        }
        KichR4StreamDrawDesc effective=desc->state;
        // The three-target GBuffer writes receiver coverage, including details
        // and GPU-skinned models. Shadow maps never enter this path.
        if(d->pass.color_count==3 && d->pass.depth && draw.depth_mode && effective.depth_write &&
            d->resources.Find(d->pass.depth)->info.format==KICH_R4_DEPTH24_STENCIL8)KichR4ReceiverState(effective.surface,true);
        hr=p->pipeline->Draw(d->context,d->draw_state,draw,vb->buffer,ib->buffer,cb->buffer,ib->buffer_stride,NULL,&effective,inputs,
            instances?instances->buffer:NULL,detail?detail->instance_count:0,detail?detail->first_instance:0,pose?pose->buffer:NULL,model?model->material_samplers:NULL);
        if(hr<0)return hr;
        memcpy(d->bound_textures,handles,sizeof(d->bound_textures));SnapshotDraw(d,draw,p,vb,cb,&effective,d->bound_textures,detail,model?model->pose_buffer:0);
        ++d->geometry_stats.draw_calls;
        const uint64_t tris=uint64_t(d->geometry_stats.triangles)+uint64_t(draw.index_count/3)*(detail?detail->instance_count:1);
        d->geometry_stats.triangles=tris>UINT32_MAX?UINT32_MAX:static_cast<uint32_t>(tris);
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL DrawDeferred(KichR4Handle device,const KichR4DeferredDrawDesc* desc){return DrawDeferredOrDetail(device,desc,NULL);}
int32_t KICH_R4_CALL DrawDetail(KichR4Handle device,const KichR4DetailDrawDesc* desc){
    if(!desc || desc->struct_size!=sizeof(*desc))return KICH_R4_E_ARGUMENT;
    return DrawDeferredOrDetail(device,&desc->surface,desc);
}
int32_t KICH_R4_CALL DrawModel(KichR4Handle device,const KichR4ModelDrawDesc* desc){
    if(!desc || desc->struct_size!=sizeof(*desc))return KICH_R4_E_ARGUMENT;
    return DrawDeferredOrDetail(device,&desc->surface,NULL,desc);
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
    if(kich_r4_dds::UploadBytes(image)>UINT32_MAX)return KICH_R4_E_MEMORY;
    return Protect([&]() -> int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        std::unique_ptr<Resource> r(new Resource());
        D3D11_TEXTURE2D_DESC native={};native.Width=image.width;native.Height=image.height;native.MipLevels=image.count;
        native.ArraySize=image.faces;native.MiscFlags=image.faces==6?D3D11_RESOURCE_MISC_TEXTURECUBE:0;native.Format=DdsFormat(image.format);native.SampleDesc.Count=1;
        native.Usage=D3D11_USAGE_IMMUTABLE;native.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial[90]={};
        for(uint32_t i=0;i<image.count*image.faces;++i){initial[i].pSysMem=image.mips[i].data;initial[i].SysMemPitch=image.mips[i].row_pitch;initial[i].SysMemSlicePitch=image.mips[i].bytes;}
        /* Normalize legacy luminance/BGRA/XRGB to the mandatory RGBA8 format;
         * preserve grayscale and opaque X bits without a BGRA device flag. */
        std::vector<std::vector<unsigned char> > converted;
        if(image.format!=kich_r4_dds::RGBA && image.format<kich_r4_dds::BC1){native.Format=DXGI_FORMAT_R8G8B8A8_UNORM;converted.resize(image.count*image.faces);
            const uint32_t source_pixel=image.format==kich_r4_dds::R8?1u:image.format==kich_r4_dds::RGB24?3u:image.format>=kich_r4_dds::RGB565 && image.format<=kich_r4_dds::ARGB4444?2u:4u;
            for(uint32_t m=0;m<image.count*image.faces;++m){const kich_r4_dds::Mip& mip=image.mips[m];const uint32_t lod=m%image.count;
                const uint32_t w=std::max(1u,image.width>>lod),h=std::max(1u,image.height>>lod);
                std::vector<unsigned char>& rgba=converted[m];rgba.resize(size_t(w)*h*4);
                for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)kich_r4_dds::ToRGBA(image.format,mip.data+size_t(y)*mip.row_pitch+x*source_pixel,rgba.data()+(size_t(y)*w+x)*4);
                initial[m].pSysMem=rgba.data();initial[m].SysMemPitch=w*4;initial[m].SysMemSlicePitch=static_cast<UINT>(rgba.size());}}
        hr=d->hw->CreateTexture2D(&native,initial,&r->texture);if(FAILED(hr))return hr;
        hr=d->hw->CreateShaderResourceView(r->texture,NULL,&r->srv);if(FAILED(hr))return hr;
        r->info.kind=KICH_R4_TEXTURE_2D;r->info.format=image.format>=kich_r4_dds::BC1?image.format:static_cast<uint32_t>(KICH_R4_RGBA8);r->info.width=image.width;r->info.height=image.height;
        r->initialized=true;r->info.byte_size=static_cast<uint32_t>(kich_r4_dds::UploadBytes(image));r->info.bindings=KICH_R4_SHADER_READ;
        *out=d->Add(std::move(r));return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL UploadBGRA8(KichR4Handle device,KichR4Handle previous,uint32_t w,uint32_t h,const void* pixels,uint32_t pitch,uint32_t bytes,KichR4Handle* out){
    if(!out)return KICH_R4_E_ARGUMENT;
    *out=0;
    if(!pixels || !w || !h || w>16384 || h>16384 || uint64_t(w)*4>pitch || uint64_t(h-1)*pitch+uint64_t(w)*4>bytes)return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t {
        int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Resource* r=previous?d->resources.Find(previous):NULL;
        if(previous && (!r || !r->texture))return KICH_R4_E_HANDLE;
        std::unique_ptr<Resource> created;
        D3D11_TEXTURE2D_DESC desc={};if(r)r->texture->GetDesc(&desc);
        if(!r || desc.Width!=w || desc.Height!=h || desc.Usage!=D3D11_USAGE_DYNAMIC || desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM){
            created.reset(new Resource());r=created.get();desc={};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            hr=d->hw->CreateTexture2D(&desc,NULL,&r->texture);if(FAILED(hr))return hr;
            hr=d->hw->CreateShaderResourceView(r->texture,NULL,&r->srv);if(FAILED(hr))return hr;
            r->info.kind=KICH_R4_TEXTURE_2D;r->info.format=KICH_R4_RGBA8;r->info.width=w;r->info.height=h;
            r->info.byte_size=w*h*4;r->info.bindings=KICH_R4_SHADER_READ;
        }
        D3D11_MAPPED_SUBRESOURCE mapped={};hr=d->context->Map(r->texture,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
        for(uint32_t y=0;y<h;++y){const unsigned char* src=static_cast<const unsigned char*>(pixels)+size_t(y)*pitch;
            unsigned char* dst=static_cast<unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch;
            for(uint32_t x=0;x<w;++x){dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];}}
        d->context->Unmap(r->texture,0);r->initialized=true;
        if(created)*out=d->Add(std::move(created));else{d->Write(r,1);*out=previous;}
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL ReadNativePixels(KichR4Handle device,void* output,uint32_t bytes,uint32_t* width,uint32_t* height){
    return Protect([&]() -> int32_t {int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        if(output)d->Emit("GPU_COPY","screenshot","operation","BACKBUFFER_TO_STAGING_COPYRESOURCE_AND_CPU_MAP; USER_SCREENSHOT_PATH");
        return d->frame_driver.ReadPixels(output,bytes,width,height);});
}
int32_t KICH_R4_CALL SetSnapshotSink(KichR4Handle device,KichR4SnapshotSink sink){
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;d->snapshot_sink=sink;return KICH_R4_OK;});
}
int32_t KICH_R4_CALL ResetNativeResources(KichR4Handle device,KichR4ResourceStats* out,uint32_t bytes){
    if((out && !OutputOK(out,bytes)) || (!out && bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        if((d->frame.Stats().flags&KICH_R4_FRAME_OPEN) || d->profile_open)return KICH_R4_E_STATE;
        d->ClearResources();memset(&d->pass,0,sizeof(d->pass));
        for(unsigned i=0;i<4;++i)d->profile[i].Reset();
        d->profile_write=0;d->profile_open=false;
        // Submit deferred releases after dropping all scene references; keep the swap chain.
        d->context->Flush();
        if(out)*out=d->stats;
        return KICH_R4_OK;
    });
}
int32_t KICH_R4_CALL NativeProfileMark(KichR4Handle device,uint32_t marker){
    if(marker>7)return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        Device::ProfileSlot& p=d->profile[d->profile_write];
        if(marker==0){d->capture_query_requested=true;if(p.pending){d->TimingResult(d->telemetry,KICH_R4_TIMING_TIMEOUT,0);d->Emit("GPU_TIMING","query_ring","skip_reason","CURRENT_RING_SLOT_PENDING; NO_WAIT");return S_FALSE;}
            p.origin=d->telemetry;p.valid_mask=(d->telemetry.flags&KICH_R4_TELEMETRY_MENU)?1:127;p.poll_count=0;p.status=KICH_R4_TIMING_NONE;
            if(!p.disjoint){D3D11_QUERY_DESC q={D3D11_QUERY_TIMESTAMP_DISJOINT,0};hr=d->hw->CreateQuery(&q,&p.disjoint);if(FAILED(hr)){d->TimingResult(p.origin,KICH_R4_TIMING_UNSUPPORTED,0);return hr;}
                q.Query=D3D11_QUERY_TIMESTAMP;for(unsigned i=0;i<8;++i){hr=d->hw->CreateQuery(&q,&p.stamp[i]);if(FAILED(hr)){d->TimingResult(p.origin,KICH_R4_TIMING_UNSUPPORTED,0);p.Reset();return hr;}}}
            d->context->Begin(p.disjoint);d->profile_open=true;
        }
        if(!d->profile_open)return S_FALSE;
        d->context->End(p.stamp[marker]);
        if(marker==7){d->context->End(p.disjoint);p.pending=true;d->profile_open=false;d->profile_write=(d->profile_write+1)%4;}
        return S_OK;
    });
}
int32_t KICH_R4_CALL GetNativeProfile(KichR4Handle device,KichR4GPUProfile* out,uint32_t bytes){
    if(!out || bytes<sizeof(*out))return KICH_R4_E_ARGUMENT;
    return Protect([&]() -> int32_t {int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);
        for(unsigned j=0;j<4;++j){Device::ProfileSlot& p=d->profile[(d->profile_write+j)%4];if(!p.pending || p.origin.capture_id)continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing={};if(d->context->GetData(p.disjoint,&timing,sizeof(timing),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
            UINT64 stamp[8]={};bool ready=true;for(unsigned i=0;i<8;++i)if(d->context->GetData(p.stamp[i],&stamp[i],sizeof(stamp[i]),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)ready=false;
            if(!ready)continue;
            p.pending=false;if(timing.Disjoint || !timing.Frequency)continue;
            out->ready=1;for(unsigned i=0;i<7;++i)out->milliseconds[i]=static_cast<float>(double(stamp[i+1]-stamp[i])*1000.0/double(timing.Frequency));return S_OK;
        }return S_OK;
    });
}

int32_t KICH_R4_CALL SetTelemetryCapture(KichR4Handle device,const KichR4TelemetryContext* capture,KichR4TelemetrySink sink){
    if((capture && !TelemetryContextValid(*capture)) || (!capture && sink))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        if(!capture){
            if(d->profile_open){Device::ProfileSlot& p=d->profile[d->profile_write];d->context->End(p.disjoint);d->TimingResult(p.origin,KICH_R4_TIMING_ABORTED,0);p.Reset();d->profile_open=false;}
            if(d->telemetry.capture_id && !d->capture_query_requested){d->TimingResult(d->telemetry,KICH_R4_TIMING_ABORTED,0);d->capture_query_requested=true;}
            d->Counters();d->telemetry_sink=NULL;return KICH_R4_OK;
        }
        d->telemetry=*capture;d->telemetry_sink=sink;d->capture_query_requested=false;++d->telemetry_epoch;d->pass_ordinal=0;d->draw_ordinal=0;d->pass_routes.clear();
        d->resource_records=d->resource_records_dropped=d->route_observations=d->route_records=d->route_records_dropped=0;
        // Keep context and resource-writer provenance current even when the
        // optional text sink is off; adapter queries and formatting are not
        // part of rendering or a timing-only measurement.
        if(!sink || !(capture->flags&KICH_R4_TELEMETRY_DETAILED))return KICH_R4_OK;
        d->Number("GPU_CONTEXT","device","engine_frame",capture->engine_frame);d->Number("GPU_CONTEXT","device","render_frame",capture->render_frame);
        d->Number("GPU_CONTEXT","device","native_present_attempt_ordinal",capture->present_frame);d->Number("GPU_CONTEXT","device","device_generation",capture->device_generation);d->Number("GPU_CONTEXT","device","level_generation",capture->level_generation);
        const KichR4FrameStats frame=d->frame.Stats();d->Number("GPU_BACKBUFFER","swapchain","width",frame.width);d->Number("GPU_BACKBUFFER","swapchain","height",frame.height);d->Number("GPU_BACKBUFFER","swapchain","write_generation",d->backbuffer_generation);d->Emit("GPU_BACKBUFFER","swapchain","owner","FRAME_DRIVER_SWAPCHAIN; EXCLUDED_FROM_RESOURCE_REGISTRY_PAYLOAD");
        if(d->backbuffer_write==UINT64_MAX)d->Emit("GPU_BACKBUFFER","swapchain","last_writer_engine_frame","UNKNOWN_OR_UNWRITTEN");else d->Number("GPU_BACKBUFFER","swapchain","last_writer_engine_frame",d->backbuffer_write);
        d->Number("GPU_CONTEXT","device","feature_level",d->hw->GetFeatureLevel());d->Number("GPU_CONTEXT","device","created_baseline",d->created_total);d->Number("GPU_CONTEXT","device","released_baseline",d->released_total);
        d->Emit("GPU_CONTEXT","device","timing_policy","4_SLOTS; POLL_ON_RENDER; DONOTFLUSH; MAX_120_POLLS; NO_FORCED_WAIT");
        d->Emit("GPU_CONTEXT","device","timing_stage_mapping",capture->flags&KICH_R4_TELEMETRY_MENU?"0=MENU_RENDER_TOTAL;1..6=NOT_APPLICABLE":"0=RESOURCE_INIT_UPLOAD_SHADOWS;1=GBUFFER;2=LIGHTING;3=COMBINE;4=FORWARD_DISTORT_HUD;5=POSTPROCESS_UI;6=END_MARKER_GAP");
        if(sink){Com<IDXGIDevice> dxgi;Com<IDXGIAdapter> adapter;
            if(SUCCEEDED(d->hw->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.Out()))) && SUCCEEDED(dxgi->GetAdapter(adapter.Out()))){DXGI_ADAPTER_DESC desc={};
                if(SUCCEEDED(adapter->GetDesc(&desc))){char text[384];WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,text,sizeof(text),NULL,NULL);text[sizeof(text)-1]=0;d->Emit("GPU_CONTEXT","adapter","description",text);
                    d->Number("GPU_CONTEXT","adapter","vendor_id",desc.VendorId);d->Number("GPU_CONTEXT","adapter","device_id",desc.DeviceId);d->Number("GPU_CONTEXT","adapter","dedicated_video_memory",desc.DedicatedVideoMemory);
                    LARGE_INTEGER version={};if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version)))d->Number("GPU_CONTEXT","adapter","driver_version_raw",static_cast<uint64_t>(version.QuadPart));else d->Emit("GPU_CONTEXT","adapter","driver_version_raw","NOT_AVAILABLE");}}
        }
        return KICH_R4_OK;});
}
int32_t KICH_R4_CALL PollTelemetryTiming(KichR4Handle device,KichR4TelemetryTiming* out,uint32_t bytes){
    if(!OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    return Protect([&]()->int32_t{int32_t hr;Device* d=FindDevice(device,hr);if(!d)return hr;
        memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);
        for(unsigned j=0;j<4;++j){Device::ProfileSlot& p=d->profile[j];if(!p.pending || !p.origin.capture_id)continue;
            ++p.poll_count;D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing={};uint64_t stamps[8]={};uint32_t status=KICH_R4_TIMING_NONE;double ms[7]={};
            const HRESULT disjoint_hr=d->context->GetData(p.disjoint,&timing,sizeof(timing),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(FAILED(disjoint_hr))status=KICH_R4_TIMING_UNSUPPORTED;
            else if(disjoint_hr==S_OK){bool ready=true;
                for(unsigned i=0;i<8;++i){const HRESULT stamp_hr=d->context->GetData(p.stamp[i],&stamps[i],sizeof(stamps[i]),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(FAILED(stamp_hr)){status=KICH_R4_TIMING_UNSUPPORTED;break;}if(stamp_hr!=S_OK)ready=false;}
                if(!status && ready)status=TelemetryTimestampResult(timing.Disjoint!=FALSE,timing.Frequency,stamps,p.valid_mask,ms);
            }
            if(!status && p.poll_count>=120)status=KICH_R4_TIMING_TIMEOUT;
            if(status){d->TimingResult(p.origin,status,p.valid_mask,ms);p.pending=false;memset(&p.origin,0,sizeof(p.origin));if(status==KICH_R4_TIMING_TIMEOUT || status==KICH_R4_TIMING_UNSUPPORTED)p.Reset();}
        }
        if(d->timing_count){*out=d->timing_results[0];--d->timing_count;memmove(d->timing_results,d->timing_results+1,d->timing_count*sizeof(*out));}
        return KICH_R4_OK;});
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
extern "C" int32_t KICH_R4_CALL KichR4_GetDeferredAPI(uint32_t abi,KichR4DeferredAPI* out,uint32_t bytes)
{
    if(abi!=KICH_R4_DEFERRED_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4DeferredAPI api={sizeof(api),KICH_R4_DEFERRED_ABI,sizeof(void*),0,CreateDeferredPipeline,BeginDeferredPass,DrawDeferred};
    *out=api;return KICH_R4_OK;
}
extern "C" int32_t KICH_R4_CALL KichR4_GetStreamAPI(uint32_t abi,KichR4StreamAPI* out,uint32_t bytes)
{
    if(abi!=KICH_R4_STREAM_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4StreamAPI api={sizeof(api),KICH_R4_STREAM_ABI,sizeof(void*),0,CreateStreamPipeline,DrawStream};
    *out=api;return KICH_R4_OK;
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

extern "C" int32_t KICH_R4_CALL KichR4_GetNativeToolsAPI(uint32_t abi,KichR4NativeToolsAPI* out,uint32_t bytes){
    if(abi!=KICH_R4_NATIVE_TOOLS_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4NativeToolsAPI api={sizeof(api),KICH_R4_NATIVE_TOOLS_ABI,sizeof(void*),0,ReadNativePixels,NativeProfileMark,GetNativeProfile,ResetNativeResources,SetSnapshotSink,UploadBGRA8};*out=api;return KICH_R4_OK;
}
extern "C" int32_t KICH_R4_CALL KichR4_GetDetailAPI(uint32_t abi,KichR4DetailAPI* out,uint32_t bytes){
    if(abi!=KICH_R4_DETAIL_ABI || !out || bytes<sizeof(*out))return KICH_R4_E_ARGUMENT;
    const KichR4DetailAPI api={sizeof(api),KICH_R4_DETAIL_ABI,sizeof(void*),0,CreateDetailPipeline,CreateDetailInstances,UploadDetailInstances,DrawDetail};
    *out=api;return KICH_R4_OK;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetModelAPI(uint32_t abi,KichR4ModelAPI* out,uint32_t bytes){
    if(abi!=KICH_R4_MODEL_ABI || !out || bytes<sizeof(*out))return KICH_R4_E_ARGUMENT;
    const KichR4ModelAPI api={sizeof(api),KICH_R4_MODEL_ABI,sizeof(void*),0,CreateModelPipeline,CreateModelPose,UploadModelPose,DrawModel};
    *out=api;return KICH_R4_OK;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetTelemetryAPI(uint32_t abi,KichR4TelemetryAPI* out,uint32_t bytes){
    if(abi!=KICH_R4_TELEMETRY_ABI || !OutputOK(out,bytes))return KICH_R4_E_ARGUMENT;
    const KichR4TelemetryAPI api={sizeof(api),KICH_R4_TELEMETRY_ABI,sizeof(void*),0,SetTelemetryCapture,PollTelemetryTiming};*out=api;return KICH_R4_OK;
}
