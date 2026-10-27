#ifndef KICH_R4_GEOMETRY_H
#define KICH_R4_GEOMETRY_H
#include "KichR4Frame.h"

/* B4.1 C ABI v1; reuses B2 handles and B3 devices/targets. Win32 pack(4).
 * Existing B0/B2/B3 contracts do not change. Everything is DLL-owned except
 * the caller's borrowed descriptions/data. All calls use the device thread.
 */
#define KICH_R4_GEOMETRY_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_GEOMETRY "KichR4_GetGeometryAPI"
#define KICH_R4_GRAPHICS_PIPELINE UINT32_C(7)
enum KichR4Semantic {KICH_R4_POSITION=1,KICH_R4_NORMAL=2,KICH_R4_TEXCOORD=3,KICH_R4_COLOR=4};
enum KichR4VertexFormat {KICH_R4_FLOAT2=1,KICH_R4_FLOAT3=2,KICH_R4_FLOAT4=3,KICH_R4_UNORM8X4=4};
enum KichR4CullMode {KICH_R4_CULL_BACK=0,KICH_R4_CULL_FRONT=1,KICH_R4_CULL_NONE=2};
/* KichRay R2 r2_sun_shadow_cull for sun caster passes only: 0 keeps the
 * material cull (R2 near cascade CULL_CCW), 1 none (one-sided fences, signs
 * and boards cast from both sides), 2 back faces. */
#ifdef __cplusplus
inline uint32_t KichR4SunShadowCull(uint32_t cull,int32_t mode){return mode==1?(uint32_t)KICH_R4_CULL_NONE:mode==2?(uint32_t)KICH_R4_CULL_BACK:cull;}
/* P0-S11 r4_sun_shadow_faces, under r2_sun_shadow_cull 0: the faces of
 * one-sided (back-culled) casters a sun cascade stores. 0 front faces in
 * every cascade; 1 the R2 layout: the contact cascade (R2 sun near, CULL_CCW)
 * front faces, the 128/512 m cascades (R2 sun far, CULL_CW) back faces only,
 * so a lit surface never shadows itself or its lit neighbours (whole road
 * triangles dark under a low moon); 2 back faces in every cascade. Two-sided
 * materials stay two-sided; r2_sun_shadow_cull 1/2 keep their meaning. */
inline uint32_t KichR4SunCasterCull(uint32_t cull,int32_t mode,uint32_t faces,bool contact){
    if(mode)return KichR4SunShadowCull(cull,mode);
    const bool back=faces==2 || (faces==1 && !contact);
    return back && cull==(uint32_t)KICH_R4_CULL_BACK?(uint32_t)KICH_R4_CULL_FRONT:cull;}
// The contact cascade's pass is "sun_2" (KichR4DeferredSession, P0-S2).
inline bool KichR4SunContactPhase(const char* phase){return phase && phase[0]=='s' && phase[1]=='u' && phase[2]=='n' && phase[3]=='_' && phase[4]=='2';}
#endif
enum KichR4DepthMode {KICH_R4_DEPTH_OFF=0,KICH_R4_DEPTH_ON=1};
#pragma pack(push,4)
typedef struct KichR4VertexElement {uint32_t semantic,semantic_index,format,byte_offset;} KichR4VertexElement;
typedef struct KichR4PipelineDesc {
    uint32_t struct_size,vertex_stride,element_count,flags;
    KichR4Handle vertex_shader,pixel_shader;
    KichR4VertexElement elements[8];
} KichR4PipelineDesc;
typedef struct KichR4DrawIndexedDesc {
    uint32_t struct_size;
    KichR4Handle pipeline,vertex_buffer,index_buffer,constant_buffer;
    uint32_t index_count,start_index,cull_mode,depth_mode;
} KichR4DrawIndexedDesc;
typedef struct KichR4GeometryStats {
    uint32_t struct_size,draw_calls,triangles,buffer_updates;
    uint32_t pipelines_created,pipelines_released,depth_created,depth_released;
} KichR4GeometryStats;
typedef struct KichR4GeometryReport {
    uint32_t struct_size;
    KichR4FrameStats frame;
    KichR4ResourceStats resources;
    KichR4GeometryStats geometry;
} KichR4GeometryReport;

/* v1: one vertex stream; TRIANGLELIST, BaseVertexLocation=0, clockwise fronts.
 * Pipeline owns references to VS/PS plus input/raster/depth states. Release
 * pipelines using B2 release_resource, or let destroy_device release all.
 * Shader bindings currently support only cbuffer b0 (VS/PS); no textures yet.
 * update_buffer uploads a complete B2 VB/IB/CB, copying data before return.
 * draw_indexed requires begin+clear and fully initialized buffers; index range
 * is checked against IB capacity. Caller guarantees indices refer to the VB.
 * destroy_device reports FINAL frame/resource/pipeline/depth ownership counts.
 */
typedef struct KichR4GeometryAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4PipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *update_buffer)(KichR4Handle,KichR4Handle,const void*,uint32_t);
    int32_t (KICH_R4_CALL *draw_indexed)(KichR4Handle,const KichR4DrawIndexedDesc*);
    int32_t (KICH_R4_CALL *get_stats)(KichR4Handle,KichR4GeometryStats*,uint32_t);
    int32_t (KICH_R4_CALL *destroy_device)(KichR4Handle,KichR4GeometryReport*,uint32_t);
} KichR4GeometryAPI;
#pragma pack(pop)
typedef char KichR4PipelineDesc_size[(sizeof(KichR4PipelineDesc)==160)?1:-1];
typedef char KichR4DrawIndexedDesc_size[(sizeof(KichR4DrawIndexedDesc)==52)?1:-1];
typedef char KichR4GeometryStats_size[(sizeof(KichR4GeometryStats)==32)?1:-1];
typedef char KichR4GeometryReport_size[(sizeof(KichR4GeometryReport)==112)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4GeometryAPI_x86_size[(sizeof(KichR4GeometryAPI)==36)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetGeometryAPIFn)(uint32_t,KichR4GeometryAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetGeometryAPI(uint32_t,KichR4GeometryAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
