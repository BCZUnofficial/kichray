#ifndef KICH_R4_LEVEL_H
#define KICH_R4_LEVEL_H
#include "KichR4Geometry.h"
/* B4.2 additive C ABI. B0/B2/B3/B4.1 tables remain byte-identical.
 * Texture data is borrowed for the call, copied by CreateTexture2D before
 * return. DDS v1: a 2D image, mip chain, BC1/2/3/4/5/7 or RGBA/BGRA/R8.
 * Textured pipelines support VS cbuffer b0, PS b0 / Texture2D t0 / sampler s0.
 * Resources share the B2 owner/thread/handle registry and B4.1 final cleanup.
 * The shared 32-bit geometry triangle counter saturates at UINT32_MAX.
 */
#define KICH_R4_LEVEL_ABI UINT32_C(0x00010000)
#define KICH_R4_EXPORT_LEVEL "KichR4_GetLevelAPI"
/* Additional get_info formats for DDS resources. Uncompressed DDS formats
 * are normalized to the existing KICH_R4_RGBA8 (including legacy luminance). */
enum KichR4CompressedFormat {KICH_R4_BC1=111,KICH_R4_BC2=112,KICH_R4_BC3=113,
    KICH_R4_BC4=114,KICH_R4_BC5=115,KICH_R4_BC7=117};
#pragma pack(push,4)
typedef struct KichR4DdsDesc {uint32_t struct_size,max_dimension,flags;} KichR4DdsDesc;
typedef struct KichR4TexturedDrawDesc {
    uint32_t struct_size;
    KichR4DrawIndexedDesc draw;
    KichR4Handle texture;
} KichR4TexturedDrawDesc;
typedef struct KichR4LevelAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_texture_dds)(KichR4Handle,const KichR4DdsDesc*,const void*,uint32_t,KichR4Handle*);
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4PipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *draw_textured)(KichR4Handle,const KichR4TexturedDrawDesc*);
} KichR4LevelAPI;
#pragma pack(pop)
typedef char KichR4DdsDesc_size[(sizeof(KichR4DdsDesc)==12)?1:-1];
typedef char KichR4TexturedDrawDesc_size[(sizeof(KichR4TexturedDrawDesc)==64)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4LevelAPI_x86_size[(sizeof(KichR4LevelAPI)==28)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetLevelAPIFn)(uint32_t,KichR4LevelAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetLevelAPI(uint32_t,KichR4LevelAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
