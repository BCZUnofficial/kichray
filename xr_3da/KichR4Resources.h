#ifndef KICH_R4_RESOURCES_H
#define KICH_R4_RESOURCES_H

#include "KichR4Bridge.h"

/* B2 resource ABI v1. Win32, __cdecl, pack(4); independent of B0's ABI.
 * No engine/STL/COM objects or native formats cross the DLL boundary.
 * Calls, including destruction, belong to the thread that created the device.
 * All allocations and releases occur inside the same R4 DLL. A handle has one
 * owner, is never reused during this DLL load, and expires at device destruction.
 * Destroy every device before FreeLibrary. Handles do not survive DLL reloads.
 */
#define KICH_R4_RESOURCE_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_RESOURCES "KichR4_GetResourceAPI"
#define KICH_R4_OK ((int32_t)0)
#define KICH_R4_E_ARGUMENT ((int32_t)UINT32_C(0x80070057))
#define KICH_R4_E_HANDLE ((int32_t)UINT32_C(0x80070006))
#define KICH_R4_E_MEMORY ((int32_t)UINT32_C(0x8007000e))
#define KICH_R4_E_UNSUPPORTED ((int32_t)UINT32_C(0x80004001))
#define KICH_R4_E_THREAD ((int32_t)UINT32_C(0x8001010e))

typedef uint64_t KichR4Handle; /* 0 = null; numeric token, never a COM pointer. */

enum KichR4ResourceKind {
    KICH_R4_TEXTURE_2D = 1, KICH_R4_VERTEX_BUFFER = 2,
    KICH_R4_INDEX_BUFFER = 3, KICH_R4_CONSTANT_BUFFER = 4,
    KICH_R4_VERTEX_SHADER = 5, KICH_R4_PIXEL_SHADER = 6
};
enum KichR4Format {
    KICH_R4_RGBA8 = 1, KICH_R4_RGBA8_SRGB = 2, KICH_R4_RGBA16F = 3,
    KICH_R4_R32F = 4, KICH_R4_DEPTH24_STENCIL8 = 5, KICH_R4_DEPTH32F = 6
};
enum KichR4TextureBinding {
    KICH_R4_SHADER_READ = 1, KICH_R4_COLOR_TARGET = 2, KICH_R4_DEPTH_TARGET = 4
};
enum KichR4ShaderBindingKind {
    KICH_R4_BIND_CONSTANTS = 1, KICH_R4_BIND_TEXTURE = 2, KICH_R4_BIND_SAMPLER = 3
};
enum KichR4ScalarType {
    KICH_R4_FLOAT = 1, KICH_R4_INT = 2, KICH_R4_UINT = 3, KICH_R4_BOOL = 4
};
enum KichR4MatrixLayout {
    KICH_R4_NOT_MATRIX = 0, KICH_R4_MATRIX_ROWS = 1, KICH_R4_MATRIX_COLUMNS = 2
};

#pragma pack(push, 4)
typedef struct KichR4TextureDesc {
    uint32_t struct_size, width, height, format, bindings;
} KichR4TextureDesc; /* v1: one mip, one slice, no MSAA; no file loader yet. */
typedef struct KichR4BufferDesc {
    uint32_t struct_size, kind, byte_size, stride;
} KichR4BufferDesc; /* CB: stride=0, size multiple of 16, <=65536. */
typedef struct KichR4ShaderDesc {
    uint32_t struct_size, kind;
    char entry[32];
    char name[64];
} KichR4ShaderDesc; /* SM5; source is borrowed for this call; no #include. */
typedef struct KichR4ResourceInfo {
    uint32_t struct_size, kind, format, width, height, byte_size, bindings;
    uint32_t binding_count, variable_count;
} KichR4ResourceInfo;
typedef struct KichR4ShaderBinding {
    uint32_t struct_size, kind, slot, count, byte_size, first_variable, variable_count;
    char name[64];
} KichR4ShaderBinding;
typedef struct KichR4ShaderVariable {
    uint32_t struct_size, binding_index, byte_offset, byte_size, scalar_type;
    uint32_t rows, columns, elements, matrix_layout;
    char name[64];
} KichR4ShaderVariable; /* HLSL byte offsets, not D3D9 register indices. */
typedef struct KichR4ResourceStats {
    uint32_t struct_size, live_resources, created, released;
    uint64_t payload_bytes; /* logical bytes of live textures/buffers, not VRAM. */
} KichR4ResourceStats;

/* HRESULT-style results: 0 success; negative failure. Failed creation sets
 * output handle to 0. Failed query does not modify caller output.
 * All output buffers are caller-owned. No pointer remains borrowed after return.
 * destroy_device releases outstanding children and returns final live=0 stats.
 * Shader/reflection/bytecode live together in one shader resource; no AddRef API.
 */
typedef struct KichR4ResourceAPI {
    uint32_t struct_size, abi_version, pointer_size, flags;
    int32_t (KICH_R4_CALL *create_device)(KichR4Handle*);
    int32_t (KICH_R4_CALL *destroy_device)(KichR4Handle, KichR4ResourceStats*, uint32_t);
    int32_t (KICH_R4_CALL *create_texture)(KichR4Handle, const KichR4TextureDesc*, KichR4Handle*);
    int32_t (KICH_R4_CALL *create_buffer)(KichR4Handle, const KichR4BufferDesc*, const void*, uint32_t, KichR4Handle*);
    int32_t (KICH_R4_CALL *compile_shader)(KichR4Handle, const KichR4ShaderDesc*, const char*, uint32_t, KichR4Handle*, char*, uint32_t);
    int32_t (KICH_R4_CALL *get_info)(KichR4Handle, KichR4Handle, KichR4ResourceInfo*, uint32_t);
    int32_t (KICH_R4_CALL *get_binding)(KichR4Handle, KichR4Handle, uint32_t, KichR4ShaderBinding*, uint32_t);
    int32_t (KICH_R4_CALL *get_variable)(KichR4Handle, KichR4Handle, uint32_t, KichR4ShaderVariable*, uint32_t);
    int32_t (KICH_R4_CALL *release_resource)(KichR4Handle, KichR4Handle);
    int32_t (KICH_R4_CALL *get_stats)(KichR4Handle, KichR4ResourceStats*, uint32_t);
} KichR4ResourceAPI;
#pragma pack(pop)

typedef char KichR4TextureDesc_size[(sizeof(KichR4TextureDesc) == 20) ? 1 : -1];
typedef char KichR4BufferDesc_size[(sizeof(KichR4BufferDesc) == 16) ? 1 : -1];
typedef char KichR4ShaderDesc_size[(sizeof(KichR4ShaderDesc) == 104) ? 1 : -1];
typedef char KichR4ResourceInfo_size[(sizeof(KichR4ResourceInfo) == 36) ? 1 : -1];
typedef char KichR4ShaderBinding_size[(sizeof(KichR4ShaderBinding) == 92) ? 1 : -1];
typedef char KichR4ShaderVariable_size[(sizeof(KichR4ShaderVariable) == 100) ? 1 : -1];
typedef char KichR4ResourceStats_size[(sizeof(KichR4ResourceStats) == 24) ? 1 : -1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4ResourceAPI_x86_size[(sizeof(KichR4ResourceAPI) == 56) ? 1 : -1];
#endif

typedef int32_t (KICH_R4_CALL *KichR4GetResourceAPIFn)(uint32_t, KichR4ResourceAPI*, uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetResourceAPI(uint32_t abi, KichR4ResourceAPI* api, uint32_t api_size);
#ifdef __cplusplus
}
#endif
#endif
