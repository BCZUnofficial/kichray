#ifndef KICH_R4_LIGHTING_H
#define KICH_R4_LIGHTING_H
#include <stdint.h>
#include <stddef.h>
#include <vector>
/* CPU snapshots only. type follows IRender_Light: POINT=1, SPOT=2. */
struct KichR4LocalLight {
    float position[3],range,direction[3],cone,color[3];uint32_t type;
};
enum KichR4LightRole {KICH_R4_LIGHT_ROLE_UNKNOWN=0,KICH_R4_LIGHT_ROLE_RUNTIME=1,KICH_R4_LIGHT_ROLE_LEVEL=2};
enum KichR4LightFlags {KICH_R4_LIGHT_STATIC=1u};
enum {KICH_R4_LIGHT_REGISTRY_CAPACITY=4096};
/* EXE-side provenance, kept outside the 48-byte renderer light descriptor.
 * serial identifies a live registration across activation changes; pointer
 * is explicitly process-local. Equal numeric lights remain distinct. */
struct KichR4LightIdentity {
    uint64_t process_handle,serial;
    uint32_t role,level_record,controller,flags;
};
// Native renderer installs a conservative current-camera light-volume query.
// NULL and non-camera callers (ROS/AI) preserve the complete source registry.
typedef bool (*KichR4LightVisibility)(const KichR4LocalLight&,const float* camera_vp);
struct KichR4LightingFrame {
    uint32_t valid,count,shadow_mask;
    float sun_direction[3],sun_color[3],ambient[3],hemi[3],fog[3];
    std::vector<KichR4LocalLight> lights;
    std::vector<KichR4LightIdentity> sources;
    std::vector<uint8_t> shadows;
    // Keep the camera used for selection so diagnostics explain the same set.
    float visibility_vp[16];uint32_t visibility_valid;
    // CPU-side selection provenance, never copied into GPU light constants.
    uint32_t r1_lights_requested,r1_lights_applied;
    uint32_t occlusion_enabled,occlusion_tested;
    std::vector<KichR4LightIdentity> occlusion_rejected;
    // Small direct/test producers can still fill the original first eight
    // entries. Captured frames resize to every visible registry source.
    KichR4LightingFrame():valid(0),count(0),shadow_mask(0),sun_direction(),sun_color(),ambient(),hemi(),fog(),
        lights(8),sources(8),visibility_vp(),visibility_valid(0),r1_lights_requested(0),r1_lights_applied(0),occlusion_enabled(0),occlusion_tested(0){}
    bool CastsShadow(unsigned index)const {
        if(index>=count)return false;
        if(index<shadows.size())return shadows[index]!=0;
        // Compatibility with direct frame producers; captured frames always
        // carry shadows for every source, including indices above 31.
        return index<32 && (shadow_mask&(1u<<index))!=0;
    }
};
static_assert(sizeof(KichR4LocalLight)==48,"CPU light layout");
static_assert(sizeof(KichR4LightIdentity)==32,"CPU light provenance layout");
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
KICH_R4_LIGHT_EXPORT bool KichR4_CaptureLightIdentity(const void*,const KichR4LightIdentity&);
KICH_R4_LIGHT_EXPORT void KichR4_CaptureLightForget(const void*);
#undef KICH_R4_LIGHT_EXPORT
/* EXE-only helper keeps allocation/exception handling out of the PCH adapter. */
void KichR4_CaptureLighting(const float*,KichR4LightingFrame&,bool r1_lights_enabled=false,const float* camera_vp=NULL);
void KichR4_CaptureLightingDiagnostics(const float*,const KichR4LightingFrame&);
void KichR4_SetLightVisibility(KichR4LightVisibility);
#endif
