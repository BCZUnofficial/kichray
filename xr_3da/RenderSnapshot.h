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
#pragma pack(pop)
typedef void (SNAPSHOT_CALL *RenderSnapshotSink)(const RenderSnapshotDraw*);
#if defined(_EDITOR)
/* Shared renderer sources also build in the legacy editors. They have no
 * XR_3DA snapshot owner/import library: preserve their old link boundary. */
static const bool g_render_snapshot_active=false;
static bool g_render_snapshot_console=false;
inline bool RenderSnapshot_Request(){return false;}
inline bool RenderSnapshot_Wanted(){return false;}
inline void RenderSnapshot_Begin(const RenderSnapshotInfo&){g_render_snapshot_console=false;}
inline void RenderSnapshot_End(const char*){}
inline void RenderSnapshot_Pass(const char*){}
inline void RenderSnapshot_Context(const char*,const char*,const char*){}
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
 * record events; background HOM/cache work is explicitly NOT_INSTRUMENTED. */
extern SNAPSHOT_API bool g_render_snapshot_active;
extern SNAPSHOT_API bool g_render_snapshot_console;
SNAPSHOT_API bool RenderSnapshot_Request();
SNAPSHOT_API bool RenderSnapshot_Wanted();
SNAPSHOT_API void RenderSnapshot_Begin(const RenderSnapshotInfo&);
SNAPSHOT_API void RenderSnapshot_End(const char* status);
SNAPSHOT_API void RenderSnapshot_Pass(const char*);
SNAPSHOT_API void RenderSnapshot_Context(const char* category,const char* resource,const char* material);
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
