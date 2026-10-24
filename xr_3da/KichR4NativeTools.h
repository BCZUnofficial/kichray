#ifndef KICH_R4_NATIVE_TOOLS_H
#define KICH_R4_NATIVE_TOOLS_H
#include "KichR4Frame.h"
#define KICH_R4_NATIVE_TOOLS_ABI 4u
struct RenderSnapshotDraw;
typedef void (KICH_R4_CALL *KichR4SnapshotSink)(const struct RenderSnapshotDraw*);
#pragma pack(push,4)
typedef struct KichR4GPUProfile {uint32_t struct_size,ready;float milliseconds[7];} KichR4GPUProfile;
typedef struct KichR4NativeToolsAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *read_pixels)(KichR4Handle,void*,uint32_t,uint32_t*,uint32_t*);
    int32_t (KICH_R4_CALL *profile_mark)(KichR4Handle,uint32_t);
    int32_t (KICH_R4_CALL *get_profile)(KichR4Handle,KichR4GPUProfile*,uint32_t);
    // Between frames: release scene resources and queries, retain device and window target.
    int32_t (KICH_R4_CALL *reset_resources)(KichR4Handle,KichR4ResourceStats*,uint32_t);
    int32_t (KICH_R4_CALL *set_snapshot_sink)(KichR4Handle,KichR4SnapshotSink);
    // Full BGRA8 CPU image. Reuse a compatible dynamic texture; otherwise
    // return a new handle. Caller retains previous on failure and releases it
    // after successful replacement. Borrowed pixels live only for this call.
    int32_t (KICH_R4_CALL *upload_bgra8)(KichR4Handle,KichR4Handle,uint32_t,uint32_t,const void*,uint32_t,uint32_t,KichR4Handle*);
} KichR4NativeToolsAPI;
#pragma pack(pop)
typedef int32_t (KICH_R4_CALL *KichR4GetNativeToolsAPIFn)(uint32_t,KichR4NativeToolsAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetNativeToolsAPI(uint32_t,KichR4NativeToolsAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
