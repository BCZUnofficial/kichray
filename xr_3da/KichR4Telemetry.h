#ifndef KICH_R4_TELEMETRY_H
#define KICH_R4_TELEMETRY_H
#include "KichR4Frame.h"
/* Optional diagnostics service. It does not change the renderer/draw contracts.
 * Borrowed POD records are delivered synchronously; callbacks must not re-enter
 * any R4 API. All identities are exact process-local integers, never pointers. */
#define KICH_R4_TELEMETRY_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_TELEMETRY "KichR4_GetTelemetryAPI"
#define KICH_R4_TELEMETRY_DETAILED 1u
#define KICH_R4_TELEMETRY_MENU 2u
#pragma pack(push,4)
typedef struct KichR4TelemetryContext {
    uint32_t struct_size,flags;
    uint64_t capture_id,engine_frame,render_frame,present_frame,level_generation,device_generation;
} KichR4TelemetryContext;
typedef struct KichR4TelemetryRecord {
    uint32_t struct_size,reserved;
    char category[32],id[96],key[64],value[384];
} KichR4TelemetryRecord;
typedef void (KICH_R4_CALL *KichR4TelemetrySink)(const KichR4TelemetryRecord*);
enum KichR4TimingStatus {KICH_R4_TIMING_NONE=0,KICH_R4_TIMING_READY=1,KICH_R4_TIMING_DISJOINT=2,
    KICH_R4_TIMING_TIMEOUT=3,KICH_R4_TIMING_ABORTED=4,KICH_R4_TIMING_UNSUPPORTED=5};
typedef struct KichR4TelemetryTiming {
    uint32_t struct_size,status,valid_mask,reserved;
    uint64_t capture_id,engine_frame,render_frame;
    double milliseconds[7];
} KichR4TelemetryTiming;
typedef struct KichR4TelemetryAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *set_capture)(KichR4Handle,const KichR4TelemetryContext*,KichR4TelemetrySink);
    int32_t (KICH_R4_CALL *poll_timing)(KichR4Handle,KichR4TelemetryTiming*,uint32_t);
} KichR4TelemetryAPI;
#pragma pack(pop)
typedef int32_t (KICH_R4_CALL *KichR4GetTelemetryAPIFn)(uint32_t,KichR4TelemetryAPI*,uint32_t);
typedef char KichR4TelemetryContext_size[(sizeof(KichR4TelemetryContext)==56)?1:-1];
typedef char KichR4TelemetryRecord_size[(sizeof(KichR4TelemetryRecord)==584)?1:-1];
typedef char KichR4TelemetryTiming_size[(sizeof(KichR4TelemetryTiming)==96)?1:-1];
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetTelemetryAPI(uint32_t,KichR4TelemetryAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
