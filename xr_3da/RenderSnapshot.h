#ifndef KICH_RENDER_SNAPSHOT_H
#define KICH_RENDER_SNAPSHOT_H
#include <stdint.h>
#if defined(_WIN32) && !defined(NO_ENGINE_API)
# if defined(ENGINE_BUILD)
#  define SNAPSHOT_API __declspec(dllexport)
# else
#  define SNAPSHOT_API __declspec(dllimport)
# endif
#else
# define SNAPSHOT_API
#endif
#if defined(_WIN32)
# define SNAPSHOT_CALL __cdecl
#else
# define SNAPSHOT_CALL
#endif
/* API-neutral diagnostic contract, no D3D/engine/STL allocations across DLLs.
 * All strings are fixed, terminated UTF-8/engine resource identifiers. A draw
 * means SUBMITTED, never proof of a visible object or pixel. */
#pragma pack(push,4)
struct RenderSnapshotInfo {
    uint32_t frame,width,height,console_visible,paused,hud;
    float position[3],direction[3],fov,time,game_time,wind,sun[3];
    char renderer[16],api[16],scene[16],level[128],weather[128],build[80],source_id[68],shader_id[68],adapter[128],directory[512];
};
struct RenderSnapshotDraw {
    uint32_t struct_size,submitted,topology,index_count,vertex_count,instance_count,start_index,first_instance;
    uint32_t vertex_count_known,vertex_capacity;
    uint64_t pipeline,vb,ib,cb,instances,vs,ps,textures[8];
    char vertex_shader[96],pixel_shader[96],texture_names[8][128];
    char states[768],samplers[1024],skin[32];
};
/* EXE-side provenance only: RenderSnapshotDraw and the renderer DLL ABI stay unchanged. */
struct RenderSnapshotProvenance {
    char semantic_type[32],queue[48],mesh_id[160],material_id[160],evidence[64],uv_source[48];
    char material_route_id[64],geometry_id[64],ui_widget[192],ui_origin[64];
    uint32_t flags,pass_mask,pass_mask_known,uv_count;
    float uv[8];
};
#pragma pack(pop)
enum RenderSnapshotObjectFlags { RENDER_SNAPSHOT_PARTICLE=1, RENDER_SNAPSHOT_GLOW=2 };
enum RenderSnapshotPassBits {
    RENDER_SNAPSHOT_PASS_SUN_SHADOW=1u<<0, RENDER_SNAPSHOT_PASS_LOCAL_SHADOW=1u<<1,
    RENDER_SNAPSHOT_PASS_GBUFFER=1u<<2, RENDER_SNAPSHOT_PASS_LIGHT_ACCUMULATION=1u<<3,
    RENDER_SNAPSHOT_PASS_ENVIRONMENT_COMBINE=1u<<4, RENDER_SNAPSHOT_PASS_FORWARD_TRANSPARENCY=1u<<5,
    RENDER_SNAPSHOT_PASS_HUD_UI=1u<<6, RENDER_SNAPSHOT_PASS_POSTPROCESS=1u<<7,
    RENDER_SNAPSHOT_PASS_MENU_UI=1u<<8, RENDER_SNAPSHOT_PASS_DISTORTION=1u<<9
};
typedef void (SNAPSHOT_CALL *RenderSnapshotSink)(const RenderSnapshotDraw*);
#if defined(_EDITOR)
/* Shared renderer sources also build in the legacy editors. They have no
 * XR_3DA snapshot owner/import library: preserve their old link boundary. */
static const bool g_render_snapshot_active=false;
static bool g_render_snapshot_console=false;
inline bool RenderSnapshot_Request(){return false;}
inline bool RenderSnapshot_RequestReason(const char*){return false;}
inline bool RenderSnapshot_DiagnosticActive(){return false;}
inline bool RenderSnapshot_Detailed(){return false;}
inline bool RenderSnapshot_ProfileRequest(uint32_t){return false;}
inline uint64_t RenderSnapshot_CaptureID(){return 0;}
inline uint64_t RenderSnapshot_RenderFrameID(){return 0;}
inline uint64_t RenderSnapshot_GroupID(){return 0;}
inline void RenderSnapshot_SetHotkey(uint32_t){}
inline uint32_t RenderSnapshot_GetHotkey(){return 0;}
inline void RenderSnapshot_UIProvenance(const char*,const char*){}
inline void RenderSnapshot_UIWidget(const char*,const char*){}
inline bool RenderSnapshot_GPUTiming(uint64_t,uint64_t,uint64_t,const char*,const double*,uint32_t=127){return false;}

inline bool RenderSnapshot_UICycleRequest(const char*,uint32_t=1800){return false;}
inline void RenderSnapshot_UICycleCancel(const char*){}
inline void RenderSnapshot_Abort(const char*){}
inline void RenderSnapshot_UIState(const char*,const char*,const char*,bool,bool,bool){}
inline void RenderSnapshot_UIWindow(const char*,const char*,bool){}
inline const char* RenderSnapshot_UIScreen(){return "GAME";}
inline const char* RenderSnapshot_UIRoot(){return "NONE";}
inline void RenderSnapshot_UIInput(const char*){}
inline const char* RenderSnapshot_UIInputOwner(){return "NOT_INSTRUMENTED";}
inline void RenderSnapshot_PresentID(uint64_t,bool){}
inline void RenderSnapshot_FrameIDs(uint64_t,uint64_t,uint64_t,uint64_t){}
inline void RenderSnapshot_U64(const char*,const char*,uint64_t){}
inline void RenderSnapshot_I64(const char*,const char*,int64_t){}
inline void RenderSnapshot_Record(const char*,const char*,const char*,const char*){}
inline void RenderSnapshot_RecordU64(const char*,const char*,const char*,uint64_t){}
inline void RenderSnapshot_RecordI64(const char*,const char*,const char*,int64_t){}
inline void RenderSnapshot_Event(const char*,const char*,const char*,const char*){}
inline void RenderSnapshot_MaterialRoute(const char*){}
inline void RenderSnapshot_GeometrySource(const char*){}

inline bool RenderSnapshot_Wanted(){return false;}
inline void RenderSnapshot_Begin(const RenderSnapshotInfo&){g_render_snapshot_console=false;}
inline void RenderSnapshot_End(const char*){}
inline void RenderSnapshot_Pass(const char*){}
inline void RenderSnapshot_Context(const char*,const char*,const char*){}
inline void RenderSnapshot_Object(const char*,const char*,const char*,const char*,uint32_t){}
inline void RenderSnapshot_Queue(const char*){}
inline void RenderSnapshot_PassMask(uint32_t){}
inline void RenderSnapshot_UV(const float*,uint32_t,const char*){}
inline void RenderSnapshot_GetProvenance(RenderSnapshotProvenance* p){if(p)*p=RenderSnapshotProvenance();}
inline void RenderSnapshot_SetProvenance(const RenderSnapshotProvenance*){}
inline void RenderSnapshot_Light(const char*,const char*){}
inline void RenderSnapshot_Phase(const char*){}
inline void RenderSnapshot_DetailVariant(uint32_t){}
inline void RenderSnapshot_Shaders(const char*,const char*){}
inline void RenderSnapshot_ShaderStage(bool,const char*){}
inline void RenderSnapshot_Texture(uint32_t,const char*){}
inline void RenderSnapshot_Skin(const char*){}
inline void SNAPSHOT_CALL RenderSnapshot_Draw(const RenderSnapshotDraw*){}
inline void RenderSnapshot_Note(const char*,const char*,const char*,const char*){}
inline void RenderSnapshot_Value(const char*,const char*,double){}
inline void RenderSnapshot_Text(const char*,const char*,const char*){}
inline const char* RenderSnapshot_LastPath(){return "NOT_AVAILABLE_IN_EDITOR";}
inline void RenderSnapshot_EngineBegin(){}
inline void RenderSnapshot_EngineEnd(){}
inline void RenderSnapshot_D3D9Draw(void*,void*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,bool,int32_t){}
#else
/* Cheap data flag is false on all ordinary frames. Only the render thread may
 * mutate capture state. DiagnosticActive and bounded Event are thread-safe;
 * background HOM/cache draw recording remains NOT_INSTRUMENTED. */
extern SNAPSHOT_API bool g_render_snapshot_active;
extern SNAPSHOT_API bool g_render_snapshot_console;
SNAPSHOT_API bool RenderSnapshot_Request();
SNAPSHOT_API bool RenderSnapshot_RequestReason(const char* reason);
SNAPSHOT_API bool RenderSnapshot_DiagnosticActive();
SNAPSHOT_API bool RenderSnapshot_Detailed();
SNAPSHOT_API bool RenderSnapshot_ProfileRequest(uint32_t frames);
SNAPSHOT_API uint64_t RenderSnapshot_CaptureID();
SNAPSHOT_API uint64_t RenderSnapshot_RenderFrameID();
SNAPSHOT_API uint64_t RenderSnapshot_GroupID();
SNAPSHOT_API void RenderSnapshot_SetHotkey(uint32_t dik);
SNAPSHOT_API uint32_t RenderSnapshot_GetHotkey();
SNAPSHOT_API void RenderSnapshot_UIProvenance(const char* widget,const char* origin);
SNAPSHOT_API void RenderSnapshot_UIWidget(const char* widget,const char* origin);
/* Late timing completion appends only timing to the exact captured frame file.
 * At most 16 file identities are retained; false means no matching writable file. */
SNAPSHOT_API bool RenderSnapshot_GPUTiming(uint64_t capture_id,uint64_t engine_frame,uint64_t render_frame,const char* status,const double* milliseconds7,uint32_t valid_mask=127);

SNAPSHOT_API bool RenderSnapshot_UICycleRequest(const char* target,uint32_t max_render_frames=1800);
SNAPSHOT_API void RenderSnapshot_UICycleCancel(const char* reason);
SNAPSHOT_API void RenderSnapshot_Abort(const char* reason);
/* UIState is called once at each render boundary, including paused UI frames. */
SNAPSHOT_API void RenderSnapshot_UIState(const char* screen,const char* root,const char* input_owner,bool level_loaded,bool console_visible,bool paused);
SNAPSHOT_API void RenderSnapshot_UIWindow(const char* kind,const char* id,bool shown);
SNAPSHOT_API const char* RenderSnapshot_UIScreen();
SNAPSHOT_API const char* RenderSnapshot_UIRoot();
SNAPSHOT_API void RenderSnapshot_UIInput(const char* owner);
SNAPSHOT_API const char* RenderSnapshot_UIInputOwner();
/* Actual presentation result; successful ordinal is not the engine/render ordinal. */
SNAPSHOT_API void RenderSnapshot_PresentID(uint64_t successful_present_ordinal,bool submitted);
/* UINT64_MAX preserves an individual existing frame/generation value. */
SNAPSHOT_API void RenderSnapshot_FrameIDs(uint64_t render_frame,uint64_t present_frame,uint64_t level_generation,uint64_t device_generation);
SNAPSHOT_API void RenderSnapshot_U64(const char* section,const char* key,uint64_t value);
SNAPSHOT_API void RenderSnapshot_I64(const char* section,const char* key,int64_t value);
/* Bounded per-category dictionaries: first admitted IDs, last value per field.
 * Record is frame-only; Event spans an explicitly active UI diagnostic cycle. */
SNAPSHOT_API void RenderSnapshot_Record(const char* category,const char* id,const char* key,const char* value);
SNAPSHOT_API void RenderSnapshot_RecordU64(const char* category,const char* id,const char* key,uint64_t value);
SNAPSHOT_API void RenderSnapshot_RecordI64(const char* category,const char* id,const char* key,int64_t value);
SNAPSHOT_API void RenderSnapshot_Event(const char* kind,const char* subject,const char* action,const char* result);
SNAPSHOT_API void RenderSnapshot_MaterialRoute(const char* id);
SNAPSHOT_API void RenderSnapshot_GeometrySource(const char* id);

SNAPSHOT_API bool RenderSnapshot_Wanted();
SNAPSHOT_API void RenderSnapshot_Begin(const RenderSnapshotInfo&);
SNAPSHOT_API void RenderSnapshot_End(const char* status);
SNAPSHOT_API void RenderSnapshot_Pass(const char*);
SNAPSHOT_API void RenderSnapshot_Context(const char* category,const char* resource,const char* material);
SNAPSHOT_API void RenderSnapshot_Object(const char* semantic,const char* mesh_id,const char* material_id,const char* evidence,uint32_t flags);
SNAPSHOT_API void RenderSnapshot_Queue(const char*);
SNAPSHOT_API void RenderSnapshot_PassMask(uint32_t);
SNAPSHOT_API void RenderSnapshot_UV(const float* pairs,uint32_t count,const char* source);
SNAPSHOT_API void RenderSnapshot_GetProvenance(RenderSnapshotProvenance*);
SNAPSHOT_API void RenderSnapshot_SetProvenance(const RenderSnapshotProvenance*);
SNAPSHOT_API void RenderSnapshot_Light(const char* id,const char* scope);
SNAPSHOT_API void RenderSnapshot_Phase(const char* id);
SNAPSHOT_API void RenderSnapshot_DetailVariant(uint32_t variant);
SNAPSHOT_API void RenderSnapshot_Shaders(const char* vs,const char* ps);
SNAPSHOT_API void RenderSnapshot_ShaderStage(bool vertex,const char* name);
SNAPSHOT_API void RenderSnapshot_Texture(uint32_t stage,const char* name);
SNAPSHOT_API void RenderSnapshot_Skin(const char* mode);
SNAPSHOT_API void SNAPSHOT_CALL RenderSnapshot_Draw(const RenderSnapshotDraw*);
SNAPSHOT_API void RenderSnapshot_Note(const char* status,const char* category,const char* resource,const char* reason);
SNAPSHOT_API void RenderSnapshot_Value(const char* section,const char* name,double value);
SNAPSHOT_API void RenderSnapshot_Text(const char* section,const char* name,const char* value);
SNAPSHOT_API const char* RenderSnapshot_LastPath();
/* Engine adapter: called from device and D3D9 backend, not from the DX11 DLL. */
void RenderSnapshot_EngineBegin();
void RenderSnapshot_EngineEnd();
SNAPSHOT_API void RenderSnapshot_D3D9Draw(void* vb,void* ib,uint32_t stride,uint32_t topology,uint32_t vertices,uint32_t first,uint32_t primitives,bool indexed,int32_t result);
#endif
#undef SNAPSHOT_API
#endif
