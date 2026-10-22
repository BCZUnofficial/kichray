#ifndef KICH_R4_LIGHTING_H
#define KICH_R4_LIGHTING_H
#include <stdint.h>
/* CPU snapshots only. type follows IRender_Light: POINT=1, SPOT=2. */
struct KichR4LocalLight {
    float position[3],range,direction[3],cone,color[3];uint32_t type;
};
enum KichR4LightRole {KICH_R4_LIGHT_ROLE_UNKNOWN=0,KICH_R4_LIGHT_ROLE_RUNTIME=1,KICH_R4_LIGHT_ROLE_LEVEL=2};
/* EXE-side provenance, kept outside the 48-byte renderer light descriptor.
 * serial identifies a live registration across activation changes; pointer
 * is explicitly process-local. Equal numeric lights remain distinct. */
struct KichR4LightIdentity {
    uint64_t process_handle,serial;
    uint32_t role,level_record,controller,reserved;
};
struct KichR4LightingFrame {
    uint32_t valid,count,shadow_mask;
    float sun_direction[3],sun_color[3],ambient[3],hemi[3],fog[3];
    KichR4LocalLight lights[8];
    KichR4LightIdentity sources[8];
};
static_assert(sizeof(KichR4LocalLight)==48,"CPU light layout");
#if defined(_WIN32)
#if defined(ENGINE_BUILD)
#define KICH_R4_LIGHT_EXPORT __declspec(dllexport)
#else
#define KICH_R4_LIGHT_EXPORT __declspec(dllimport)
#endif
#else
#define KICH_R4_LIGHT_EXPORT
#endif
KICH_R4_LIGHT_EXPORT void KichR4_CaptureLight(const void*,const KichR4LocalLight&,bool);
KICH_R4_LIGHT_EXPORT void KichR4_CaptureLightIdentity(const void*,const KichR4LightIdentity&);
KICH_R4_LIGHT_EXPORT void KichR4_CaptureLightForget(const void*);
#undef KICH_R4_LIGHT_EXPORT
/* EXE-only helper keeps allocation/exception handling out of the PCH adapter. */
void KichR4_CaptureLighting(const float*,KichR4LightingFrame&);
void KichR4_CaptureLightingDiagnostics(const float*,const KichR4LightingFrame&);
#endif
