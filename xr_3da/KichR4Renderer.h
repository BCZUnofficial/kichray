#ifndef KICH_R4_RENDERER_H
#define KICH_R4_RENDERER_H
#include "KichR4Deferred.h"
#include "KichR4NativeTools.h"

/* G1 entry point. C ABI, pack(4), __cdecl; no engine/STL/D3D objects.
 * This revision advertises the transitional implementation explicitly. It
 * consolidates the existing typed services; it does NOT claim GPU model
 * submission, R2 parity, or removal of the CPU capture path.
 * Tables are copied to caller storage. Function pointers expire at FreeLibrary.
 * All handles belong to this DLL/device and the creating thread. Create the
 * device/target through frame; release resources through resources, reset a
 * scene through tools.reset_resources BETWEEN frames, and finally destroy via
 * geometry.destroy_device (which returns the combined final resource report).
 * The alternative destroy functions are for old clients, not extra owners.
 * After a successful reset, discard every old EXE-side scene handle before
 * submitting another operation; the device/target handles remain valid.
 * Suspend/occlusion are positive frame statuses, not successful draws.
 * All buffers/strings are borrowed only for a call; output storage is caller
 * owned. No exceptions or CRT allocations may cross this boundary.
 */
#define KICH_R4_RENDERER_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_RENDERER "KichR4_GetRendererAPI"
#define KICH_R4_RENDERER_TRANSITIONAL UINT32_C(1)

#pragma pack(push,4)
typedef struct KichR4BuildInfo {
    uint32_t struct_size, pointer_size, dirty, compiler_version;
    /* dirty: 0 clean, 1 modified/untracked sources, 2 Git unavailable/archive. */
    char revision[48], source_id[68], shader_id[68], compiled[40], configuration[48];
} KichR4BuildInfo;
typedef struct KichR4RendererAPI {
    uint32_t struct_size, abi_version, pointer_size, flags;
    KichR4BuildInfo build;
    KichR4FrameAPI frame;
    KichR4ResourceAPI resources;
    KichR4GeometryAPI geometry;
    KichR4LevelAPI level;
    KichR4StreamAPI stream;
    KichR4DeferredAPI deferred;
    KichR4NativeToolsAPI tools;
} KichR4RendererAPI;
#pragma pack(pop)
typedef char KichR4BuildInfo_size[(sizeof(KichR4BuildInfo)==288)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4RendererAPI_x86_size[(sizeof(KichR4RendererAPI)==560)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetRendererAPIFn)(uint32_t,KichR4RendererAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
/* Invalid requests leave output untouched. No device is created by this call. */
int32_t KICH_R4_CALL KichR4_GetRendererAPI(uint32_t,KichR4RendererAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
