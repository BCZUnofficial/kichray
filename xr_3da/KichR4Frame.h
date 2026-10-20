#ifndef KICH_R4_FRAME_H
#define KICH_R4_FRAME_H

#include "KichR4Resources.h"

/* B3 C ABI. Shares B2 device ownership/handles; never exposes graphics objects.
 * All calls and the borrowed HWND belong to the device's creating thread.
 * Destroy the target/device BEFORE destroying the window or unloading R4.
 * B0/B2 ABI versions and layouts are unchanged. Win32, pack(4), __cdecl.
 */
#define KICH_R4_FRAME_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_FRAME "KichR4_GetFrameAPI"
#define KICH_R4_E_STATE ((int32_t)UINT32_C(0x8000ffff))
#define KICH_R4_FRAME_SUSPENDED ((int32_t)1)
#define KICH_R4_FRAME_OCCLUDED ((int32_t)2)

enum KichR4FrameFlags {
    KICH_R4_FRAME_TARGET = 1, KICH_R4_FRAME_PAUSED = 2,
    KICH_R4_FRAME_OPEN = 4, KICH_R4_FRAME_HIDDEN = 8,
    KICH_R4_FRAME_VIEW = 16, KICH_R4_FRAME_FAILED = 32
};

#pragma pack(push,4)
typedef struct KichR4FrameTargetDesc {
    uint32_t struct_size, width, height;
    void* window; /* Borrowed HWND; no transfer of window ownership. */
} KichR4FrameTargetDesc;
typedef struct KichR4FrameColor {
    uint32_t struct_size;
    float rgba[4]; /* Finite values in [0,1]. */
} KichR4FrameColor;
typedef struct KichR4FrameStats {
    uint32_t struct_size, width, height, flags;
    uint32_t presents, present_attempts, clears, resizes, occlusions;
    uint32_t views_created, views_released;
    int32_t last_hresult, device_removed_reason;
} KichR4FrameStats;

/* 0 success; negative HRESULT failure. begin_frame/present can also return
 * SUSPENDED/OCCLUDED: pump window messages and wait before trying again.
 * resize(0,anything) / resize(anything,0) suspends without ResizeBuffers.
 * Nonzero resize recreates views after unbinding/releasing old back buffers.
 * present interval is 0 or 1. A frame must begin and clear before present.
 * destroy_target is idempotent and retains counters for get_stats.
 * destroy_device returns final stats (flags=0, views_created=views_released).
 * After an actual native failure, destroy/recreate the target/device; no
 * repeated Present/Resize on a broken target. No automatic renderer selection.
 */
typedef struct KichR4FrameAPI {
    uint32_t struct_size, abi_version, pointer_size, flags;
    int32_t (KICH_R4_CALL *create_device)(KichR4Handle*);
    int32_t (KICH_R4_CALL *destroy_device)(KichR4Handle, KichR4FrameStats*, uint32_t);
    int32_t (KICH_R4_CALL *create_target)(KichR4Handle, const KichR4FrameTargetDesc*);
    int32_t (KICH_R4_CALL *destroy_target)(KichR4Handle);
    int32_t (KICH_R4_CALL *resize)(KichR4Handle, uint32_t, uint32_t);
    int32_t (KICH_R4_CALL *begin_frame)(KichR4Handle);
    int32_t (KICH_R4_CALL *clear)(KichR4Handle, const KichR4FrameColor*);
    int32_t (KICH_R4_CALL *present)(KichR4Handle, uint32_t);
    int32_t (KICH_R4_CALL *get_stats)(KichR4Handle, KichR4FrameStats*, uint32_t);
} KichR4FrameAPI;
#pragma pack(pop)

typedef char KichR4FrameColor_size[(sizeof(KichR4FrameColor)==20)?1:-1];
typedef char KichR4FrameStats_size[(sizeof(KichR4FrameStats)==52)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4FrameAPI_x86_size[(sizeof(KichR4FrameAPI)==52)?1:-1];
typedef char KichR4FrameTargetDesc_x86_size[(sizeof(KichR4FrameTargetDesc)==16)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetFrameAPIFn)(uint32_t,KichR4FrameAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetFrameAPI(uint32_t,KichR4FrameAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
