#ifndef KICH_R4_LIGHTING_H
#define KICH_R4_LIGHTING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
/* CPU snapshots only. type follows IRender_Light: POINT=1, SPOT=2. */
struct KichR4LocalLight {
    float position[3],range,direction[3],cone,color[3];uint32_t type;
};
// Owned EXE-only projector data. No strings or engine pointers cross the DLL ABI.
struct KichR4LightProjector {
    std::string texture;
    float right[3];
    KichR4LightProjector():right(){}
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
// R2 CRender::Render sun policy. DEFAULT is a direct/test producer without
// engine settings and keeps the dynamic shadowed sun.
enum KichR4SunMode {KICH_R4_SUN_DEFAULT=0,KICH_R4_SUN_DYNAMIC=1,KICH_R4_SUN_STATIC=2,KICH_R4_SUN_DISABLED=3};
struct KichR4LightingFrame {
    uint32_t valid,count,shadow_mask;
    float sun_direction[3],sun_color[3],ambient[3],hemi[3],fog[3];
    // CurrentEnv mixer output, not a second weather or distance policy.
    float fog_near,fog_far;uint32_t fog_valid;
    // R2 phase_combine hmodel() inputs, scaled exactly as L_ambient/env_color.
    // Raw ambient/hemi above remain the forward-surface (L_ambient/L_hemi) inputs.
    float env_ambient[3],env_color[4],gloss_factor;uint32_t env_valid,sun_mode;
    std::string env_textures[2];
    // R2 accum_direct m_sunmask rows (u,v), world space: sunmask.dds alpha
    // projected along the sun and drifting with the wind. Zero valid = no mask.
    float sunmask[8];uint32_t sunmask_valid;
    std::vector<KichR4LocalLight> lights;
    std::vector<KichR4LightIdentity> sources;
    std::vector<KichR4LightProjector> projectors;
    std::vector<uint8_t> shadows;
    // Keep the camera used for selection so diagnostics explain the same set.
    float visibility_vp[16];uint32_t visibility_valid;
    // CPU-side selection provenance, never copied into GPU light constants.
    uint32_t r1_lights_requested,r1_lights_applied;
    uint32_t occlusion_enabled,occlusion_tested;
    std::vector<KichR4LightIdentity> occlusion_rejected;
    // EXE-side immutable native frame plan, shared by collection and submission.
    // Zero size denotes an unplanned legacy/test producer, not a zero-sized map.
    uint32_t selection_frame,local_shadow_size,shadow_plan_faces;
    uint32_t shadow_resolution_changes,shadow_resolution_reason;
    // Small direct/test producers can still fill the original first eight
    // entries. Captured frames resize to every visible registry source.
    KichR4LightingFrame():valid(0),count(0),shadow_mask(0),sun_direction(),sun_color(),ambient(),hemi(),fog(),fog_near(0),fog_far(0),fog_valid(0),
        env_ambient(),env_color(),gloss_factor(1),env_valid(0),sun_mode(KICH_R4_SUN_DEFAULT),sunmask(),sunmask_valid(0),lights(8),sources(8),projectors(8),visibility_vp(),visibility_valid(0),r1_lights_requested(0),r1_lights_applied(0),occlusion_enabled(0),occlusion_tested(0),
        selection_frame(0),local_shadow_size(0),shadow_plan_faces(0),shadow_resolution_changes(0),shadow_resolution_reason(0){}
    bool CastsShadow(unsigned index)const {
        if(index>=count)return false;
        if(index<shadows.size())return shadows[index]!=0;
        // Compatibility with direct frame producers; captured frames always
        // carry shadows for every source, including indices above 31.
        return index<32 && (shadow_mask&(1u<<index))!=0;
    }
};
// Shared by early native selection and both presentation adapters. Copying a
// frozen lighting frame must preserve the same complete environment snapshot.
template<class Environment> inline void KichR4CopyEnvironment(KichR4LightingFrame& frame,const Environment& env){
    frame.valid=1;memcpy(frame.sun_direction,&env.sun_dir.x,12);memcpy(frame.sun_color,&env.sun_color.x,12);
    memcpy(frame.ambient,&env.ambient.x,12);memcpy(frame.hemi,&env.hemi_color.x,12);memcpy(frame.fog,&env.fog_color.x,12);
    frame.fog_near=env.fog_near;frame.fog_far=env.fog_far;frame.fog_valid=1;
}
// SoC r2_rendertarget_phase_combine.cpp: L_ambient=max(ambient*2,.001)*amb,
// env_color=(sky_color*2+EPS)*2*hemi, w=CurrentEnv.weight. Not hemi_color.
template<class Environment> inline void KichR4CopyR2Hemisphere(KichR4LightingFrame& frame,const Environment& env,float ambient_scale,float hemi_scale){
    const float* ambient=&env.ambient.x;const float* sky=&env.sky_color.x;
    for(unsigned i=0;i<3;++i){const float a=ambient[i]*2;
        frame.env_ambient[i]=(a>.001f?a:.001f)*ambient_scale;frame.env_color[i]=(sky[i]*2+.00001f)*2*hemi_scale;}
    frame.env_color[3]=env.weight;frame.env_valid=1;
}
// Light_DB sun_adapted color (sun_color*r2_sun_lumscale) and CRender::Render
// bSUN: R2FLAG_SUN && u_diffuse2s(color)>EPS; R2 static sun disables the pass.
inline uint32_t KichR4SunMode(const float* sun_color,float gloss_factor,bool sun_flag,bool r2_static){
    if(r2_static)return KICH_R4_SUN_STATIC;
    const float v=(sun_color[0]+sun_color[1]+sun_color[2])/3.f;
    const float specular=v>0?gloss_factor*(v<1?powf(v,2.f/3.f):v):0;
    return sun_flag && specular>.00001f?KICH_R4_SUN_DYNAMIC:KICH_R4_SUN_DISABLED;
}
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
void KichR4_CaptureLightProjector(const void*,const KichR4LightProjector&);
/* EXE-only: CurrentEnv plus the R2 hmodel/sun policy inputs of this frame. */
void KichR4_CopyEngineEnvironment(KichR4LightingFrame&);
void KichR4_CaptureLightingDiagnostics(const float*,const KichR4LightingFrame&);
void KichR4_SetLightVisibility(KichR4LightVisibility);
void KichR4_FreezeLighting(uint32_t engine_frame,uint32_t now_ms,const float* camera,KichR4LightingFrame&,bool r1_lights_enabled,const float* camera_vp);
bool KichR4_CopyFrozenLighting(uint32_t engine_frame,const float* camera,const float* camera_vp,KichR4LightingFrame&);
void KichR4_ResetFrozenLighting();
void KichR4_ReportLightingTransitions(uint32_t engine_frame,void (*log)(const char*));
#endif
