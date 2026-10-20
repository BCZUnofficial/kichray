#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "KichR4ResourceProbe.h"

namespace {
void LogResult(KichR4LogFn log, const char* step, int32_t result)
{
    char message[256];
    snprintf(message,sizeof(message),"[R4 bridge B2] FAIL: %s; HRESULT=0x%08X",step,static_cast<uint32_t>(result));
    log(message);
}
bool Expect(KichR4LogFn log, const char* step, int32_t result, int32_t expected=KICH_R4_OK)
{
    if (result==expected) return true;
    LogResult(log,step,result); return false;
}
bool Check(KichR4LogFn log, const char* step, bool condition)
{
    if (condition) return true;
    LogResult(log,step,static_cast<int32_t>(UINT32_C(0x8000ffff))); return false;
}
bool ValidAPI(const KichR4ResourceAPI& api)
{
    return api.struct_size==sizeof(api) && api.abi_version==KICH_R4_RESOURCE_ABI &&
        api.pointer_size==sizeof(void*) && api.flags==0 && api.create_device && api.destroy_device &&
        api.create_texture && api.create_buffer && api.compile_shader && api.get_info &&
        api.get_binding && api.get_variable && api.release_resource && api.get_stats;
}
struct TestState {
    KichR4Handle device, other;
    uint32_t created, released;
};

bool TestShader(const KichR4ResourceAPI& api, TestState& state, KichR4LogFn log, uint32_t kind)
{
    const char vs[]="cbuffer Frame : register(b2) { row_major float4x4 transform; float4 tint; };"
        "float4 main(float4 p:POSITION):SV_Position { return mul(p,transform)+tint; }";
    const char ps[]="cbuffer Frame : register(b2) { float4 tint; };"
        "Texture2D image : register(t3); SamplerState linear_sampler : register(s1);"
        "float4 main(float2 uv:TEXCOORD0):SV_Target { return image.Sample(linear_sampler,uv)*tint; }";
    const char* source=kind==KICH_R4_VERTEX_SHADER ? vs : ps;
    KichR4ShaderDesc desc={}; desc.struct_size=sizeof(desc); desc.kind=kind;
    memcpy(desc.entry,"main",5); memcpy(desc.name,"kich_b2_smoke",14);
    KichR4Handle shader=0; char errors[1024]={};
    const int32_t hr=api.compile_shader(state.device,&desc,source,static_cast<uint32_t>(strlen(source)),&shader,errors,sizeof(errors));
    if (!Expect(log,kind==KICH_R4_VERTEX_SHADER ? "compile vs_5_0 (d3dcompiler_47.dll)" : "compile ps_5_0",hr)) {
        errors[sizeof(errors)-1]=0;
        if (errors[0]) { log("[R4 bridge B2] shader compiler diagnostic:"); log(errors); }
        return false;
    }
    ++state.created;
    KichR4ResourceInfo info={};
    if (!Expect(log,"shader info",api.get_info(state.device,shader,&info,sizeof(info))) ||
        !Check(log,"shader kind/table counts",info.struct_size==sizeof(info) && info.kind==kind &&
        info.binding_count==(kind==KICH_R4_VERTEX_SHADER ? 1u : 3u) &&
        info.variable_count==(kind==KICH_R4_VERTEX_SHADER ? 2u : 1u))) return false;
    bool constants=false,texture=false,sampler=false;
    for (uint32_t i=0;i<info.binding_count;++i) {
        KichR4ShaderBinding b={};
        if (!Expect(log,"shader binding",api.get_binding(state.device,shader,i,&b,sizeof(b)))) return false;
        if (!Check(log,"binding size/name/count",b.struct_size==sizeof(b) && memchr(b.name,0,sizeof(b.name)) && b.count==1)) return false;
        if (b.kind==KICH_R4_BIND_CONSTANTS) {
            const bool vertex=kind==KICH_R4_VERTEX_SHADER;
            constants=b.slot==2 && !strcmp(b.name,"Frame") && b.byte_size==(vertex ? 80u : 16u) &&
                b.variable_count==(vertex ? 2u : 1u) && b.first_variable<=info.variable_count &&
                b.variable_count<=info.variable_count-b.first_variable;
            if (!Check(log,"SM5 cbuffer slot/size",constants)) return false;
            for (uint32_t v=0;v<b.variable_count;++v) {
                KichR4ShaderVariable var={};
                if (!Expect(log,"shader variable",api.get_variable(state.device,shader,b.first_variable+v,&var,sizeof(var)))) return false;
                if (!Check(log,"variable ABI/type",var.struct_size==sizeof(var) && var.binding_index==i &&
                    var.scalar_type==KICH_R4_FLOAT && var.elements==0 && memchr(var.name,0,sizeof(var.name)))) return false;
                const bool matrix=vertex && v==0;
                if (!Check(log,"SM5 byte offsets/matrix layout",!strcmp(var.name,matrix ? "transform" : "tint") &&
                    var.byte_offset==(matrix ? 0u : vertex ? 64u : 0u) && var.byte_size==(matrix ? 64u : 16u) &&
                    var.rows==(matrix ? 4u : 1u) && var.columns==4 &&
                    var.matrix_layout==(matrix ? KICH_R4_MATRIX_ROWS : KICH_R4_NOT_MATRIX))) return false;
            }
        } else if (b.kind==KICH_R4_BIND_TEXTURE) texture=b.slot==3 && !strcmp(b.name,"image");
        else if (b.kind==KICH_R4_BIND_SAMPLER) sampler=b.slot==1 && !strcmp(b.name,"linear_sampler");
        else return Check(log,"unexpected binding kind",false);
    }
    if (!Check(log,"shader bindings complete",constants && (kind==KICH_R4_VERTEX_SHADER || (texture && sampler)))) return false;
    if (!Expect(log,"release shader/table/bytecode",api.release_resource(state.device,shader))) return false;
    ++state.released;
    return true;
}

bool RunChecks(const KichR4ResourceAPI& api, TestState& state, KichR4LogFn log)
{
    if (!Expect(log,"create hardware device",api.create_device(&state.device)) ||
        !Check(log,"device handle",state.device!=0)) return false;
    KichR4Handle retained=0;
    for (uint32_t format=KICH_R4_RGBA8;format<=KICH_R4_DEPTH32F;++format) {
        const uint32_t bindings=KICH_R4_SHADER_READ |
            (format>=KICH_R4_DEPTH24_STENCIL8 ? KICH_R4_DEPTH_TARGET : KICH_R4_COLOR_TARGET);
        KichR4TextureDesc desc={sizeof(desc),64,32,format,bindings};
        KichR4Handle handle=0;
        if (!Expect(log,"create texture/SRV/RTV-or-DSV",api.create_texture(state.device,&desc,&handle))) return false;
        ++state.created;
        KichR4ResourceInfo info={};
        if (!Expect(log,"texture info",api.get_info(state.device,handle,&info,sizeof(info))) ||
            !Check(log,"texture format/size",handle!=0 && info.struct_size==sizeof(info) && info.kind==KICH_R4_TEXTURE_2D &&
                info.format==format && info.width==64 && info.height==32 && info.bindings==bindings &&
                info.byte_size==64u*32u*(format==KICH_R4_RGBA16F ? 8u : 4u))) return false;
        if (format==KICH_R4_RGBA8) retained=handle; /* Test automatic child destruction. */
        else {
            if (!Expect(log,"release texture/views",api.release_resource(state.device,handle))) return false;
            ++state.released;
            if (!Expect(log,"reject double release",api.release_resource(state.device,handle),KICH_R4_E_HANDLE)) return false;
        }
    }
    log("[R4 bridge B2] textures: 6 formats; color SRV/RTV and depth SRV/DSV OK");
    const uint32_t data[20]={};
    for (uint32_t kind=KICH_R4_VERTEX_BUFFER;kind<=KICH_R4_CONSTANT_BUFFER;++kind) {
        const uint32_t stride=kind==KICH_R4_VERTEX_BUFFER ? 16 : kind==KICH_R4_INDEX_BUFFER ? 4 : 0;
        KichR4BufferDesc desc={sizeof(desc),kind,sizeof(data),stride}; KichR4Handle buffer=0;
        if (!Expect(log,"create vertex/index/constant buffer",api.create_buffer(state.device,&desc,data,sizeof(data),&buffer))) return false;
        ++state.created;
        KichR4ResourceInfo info={};
        if (!Expect(log,"buffer info",api.get_info(state.device,buffer,&info,sizeof(info))) ||
            !Check(log,"buffer kind/size",info.kind==kind && info.byte_size==sizeof(data))) return false;
        if (!Expect(log,"release buffer",api.release_resource(state.device,buffer))) return false;
        ++state.released;
    }
    KichR4Handle invalid=123;
    KichR4BufferDesc bad_cb={sizeof(bad_cb),KICH_R4_CONSTANT_BUFFER,15,0};
    if (!Expect(log,"reject unaligned constant buffer",api.create_buffer(state.device,&bad_cb,NULL,0,&invalid),KICH_R4_E_ARGUMENT) ||
        !Check(log,"failed buffer has null handle",invalid==0)) return false;
    KichR4TextureDesc bad_texture={sizeof(bad_texture),64,32,KICH_R4_DEPTH32F,KICH_R4_COLOR_TARGET};
    invalid=123;
    if (!Expect(log,"reject depth as color",api.create_texture(state.device,&bad_texture,&invalid),KICH_R4_E_ARGUMENT) ||
        !Check(log,"failed texture has null handle",invalid==0)) return false;
    log("[R4 bridge B2] buffers: vertex/index/constant; descriptor validation OK");
    if (!TestShader(api,state,log,KICH_R4_VERTEX_SHADER) || !TestShader(api,state,log,KICH_R4_PIXEL_SHADER)) return false;
    log("[R4 bridge B2] shaders: vs_5_0/ps_5_0; cbuffer offsets, matrix layout and binding slots OK");
    if (!Expect(log,"create second owner",api.create_device(&state.other)) ||
        !Check(log,"separate device handles",state.other && state.other!=state.device)) return false;
    if (!Expect(log,"reject resource in another device",api.release_resource(state.other,retained),KICH_R4_E_HANDLE)) return false;
    KichR4ResourceStats empty={};
    if (!Expect(log,"destroy second owner",api.destroy_device(state.other,&empty,sizeof(empty)))) return false;
    state.other=0;
    if (!Check(log,"second owner empty",empty.struct_size==sizeof(empty) && !empty.live_resources && !empty.created && !empty.released)) return false;
    KichR4ResourceStats stats={};
    if (!Expect(log,"query ownership counters",api.get_stats(state.device,&stats,sizeof(stats))) ||
        !Check(log,"ownership accounting",stats.struct_size==sizeof(stats) && stats.created==state.created &&
            stats.released==state.released && stats.live_resources==1 && stats.payload_bytes==64u*32u*4u)) return false;
    const KichR4Handle old_device=state.device;
    if (!Expect(log,"destroy device with retained child",api.destroy_device(state.device,&stats,sizeof(stats)))) return false;
    state.device=0;
    if (!Check(log,"all native resources released",stats.struct_size==sizeof(stats) && !stats.live_resources &&
        stats.created==state.created && stats.released==stats.created && !stats.payload_bytes)) return false;
    if (!Expect(log,"reject destroyed owner",api.get_stats(old_device,&stats,sizeof(stats)),KICH_R4_E_HANDLE)) return false;
    char text[192];
    snprintf(text,sizeof(text),"[R4 bridge B2] PASS: resources created=%u released=%u live=0; owners isolated; no swap chain",
        state.created,state.created);
    log(text); return true;
}

bool GuardChecks(const KichR4ResourceAPI& api, TestState& state, KichR4LogFn log)
{
#if defined(_MSC_VER)
    __try { return RunChecks(api,state,log); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogResult(log,"resource API exception",static_cast<int32_t>(GetExceptionCode())); return false;
    }
#else
    return RunChecks(api,state,log);
#endif
}
bool Cleanup(const KichR4ResourceAPI& api, KichR4Handle device, KichR4LogFn log)
{
    if (!device) return true;
#if defined(_MSC_VER)
    __try
#endif
    { return Expect(log,"cleanup device",api.destroy_device(device,NULL,0)); }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogResult(log,"cleanup exception",static_cast<int32_t>(GetExceptionCode())); return false;
    }
#endif
}
bool FetchAPI(KichR4GetResourceAPIFn get, KichR4ResourceAPI& api, KichR4LogFn log)
{
#if defined(_MSC_VER)
    __try
#endif
    { return Expect(log,"resource ABI handshake",get(KICH_R4_RESOURCE_ABI,&api,sizeof(api))); }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogResult(log,"resource export exception",static_cast<int32_t>(GetExceptionCode())); return false;
    }
#endif
}
}

bool KichR4_ResourceSelfTest(const KichR4ResourceAPI& api, KichR4LogFn log)
{
    if (!log) return false;
    if (!ValidAPI(api)) { log("[R4 bridge B2] resource ABI/table mismatch; calls skipped"); return false; }
    TestState state={};
    const bool passed=GuardChecks(api,state,log);
    const bool other_clean=Cleanup(api,state.other,log);
    const bool device_clean=Cleanup(api,state.device,log);
    if (!passed) log("[R4 bridge B2] resource test failed; continuing normal R1/R2 selection");
    return passed && other_clean && device_clean;
}
void KichR4_CheckResourceBridge(void* module, KichR4LogFn log)
{
    log("[R4 bridge B2] begin: isolated DX11 resources; ABI v1; SM5 reflection");
    const KichR4GetResourceAPIFn get=reinterpret_cast<KichR4GetResourceAPIFn>(
        GetProcAddress(static_cast<HMODULE>(module),KICH_R4_EXPORT_RESOURCES));
    if (!get) { log("[R4 bridge B2] resource export missing; use the B2 DLL; test skipped"); return; }
    KichR4ResourceAPI api={};
    if (FetchAPI(get,api,log)) KichR4_ResourceSelfTest(api,log);
}
