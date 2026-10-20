#ifndef KICH_R4_BRIDGE_H
#define KICH_R4_BRIDGE_H

/* B0 C ABI. No engine classes, Windows handles or graphics API types. */
#include <stdint.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
#define KICH_R4_CALL __cdecl
#else
#define KICH_R4_CALL
#endif

#define KICH_R4_BRIDGE_ABI_VERSION UINT32_C(0x00010000)
#define KICH_R4_REQUIRED_FEATURE_LEVEL UINT32_C(0x0000b000) /* D3D_FEATURE_LEVEL_11_0 */
#define KICH_R4_EXPORT_VERSION "KichR4_GetBridgeVersion"
#define KICH_R4_EXPORT_PROBE "KichR4_ProbeHardware"
#define KICH_R4_EXPORT_INFO "KichR4_GetProbeInfo"

enum KichR4ProbeStatus
{
    KICH_R4_PROBE_NOT_RUN = 0,
    KICH_R4_PROBE_SUPPORTED = 1,
    KICH_R4_PROBE_RUNTIME_MISSING = 2,
    KICH_R4_PROBE_ENTRYPOINT_MISSING = 3,
    KICH_R4_PROBE_DEVICE_FAILED = 4
};

#define KICH_R4_PROBE_ADAPTER_INFO UINT32_C(1)

/* Fixed-size, caller-owned data. Strings are NUL-terminated UTF-8. */
#pragma pack(push, 4)
typedef struct KichR4ProbeInfo
{
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t status;
    int32_t hresult;
    uint32_t feature_level;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t dedicated_video_memory_mb;
    uint32_t flags;
    char adapter_name[128];
} KichR4ProbeInfo;
#pragma pack(pop)

typedef char KichR4ProbeInfo_size_must_be_164[(sizeof(KichR4ProbeInfo) == 164) ? 1 : -1];

typedef uint32_t (KICH_R4_CALL *KichR4GetBridgeVersionFn)(void);
/* 1 = a hardware FL11_0 device was created; 0 = unavailable. No WARP fallback. */
typedef int32_t (KICH_R4_CALL *KichR4ProbeHardwareFn)(void);
/* Returns 1 on copy, 0 for NULL/undersized output. Never allocates for the caller. */
typedef int32_t (KICH_R4_CALL *KichR4GetProbeInfoFn)(KichR4ProbeInfo*, uint32_t);

#ifdef __cplusplus
extern "C" {
#endif
uint32_t KICH_R4_CALL KichR4_GetBridgeVersion(void);
int32_t KICH_R4_CALL KichR4_ProbeHardware(void);
int32_t KICH_R4_CALL KichR4_GetProbeInfo(KichR4ProbeInfo* info, uint32_t info_size);
#ifdef __cplusplus
}
#endif

#endif
