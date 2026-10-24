#ifndef KICH_R4_STREAM_H
#define KICH_R4_STREAM_H
#include "KichR4Level.h"
#include "KichR4SurfaceState.h"
/* Additive B4.3b C ABI. CPU geometry is uploaded with the existing buffer API.
 * No DX9 object, engine object, STL allocation or bone pointer crosses this ABI.
 * Blend factors use the D3D9 numeric values ZERO..INVSRCALPHA (1..6),
 * DESTALPHA/INVDESTALPHA/DESTCOLOR/INVDESTCOLOR (7..10). */
#define KICH_R4_STREAM_ABI UINT32_C(0x00020000)
#define KICH_R4_EXPORT_STREAM "KichR4_GetStreamAPI"
#pragma pack(push,4)
typedef struct KichR4StreamDrawDesc {
    uint32_t struct_size;
    KichR4DrawIndexedDesc draw;
    KichR4Handle texture;
    uint32_t blend_enable,source_blend,dest_blend,clamp_texture,scissor_enable,depth_write;
    int32_t scissor[4];
    float min_depth,max_depth;
    KichR4SurfaceState surface;
} KichR4StreamDrawDesc;
typedef struct KichR4StreamAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4PipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *draw)(KichR4Handle,const KichR4StreamDrawDesc*);
} KichR4StreamAPI;
#pragma pack(pop)
typedef char KichR4StreamDrawDesc_size[(sizeof(KichR4StreamDrawDesc)==496)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4StreamAPI_x86_size[(sizeof(KichR4StreamAPI)==24)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetStreamAPIFn)(uint32_t,KichR4StreamAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetStreamAPI(uint32_t,KichR4StreamAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
