#ifndef KICH_R4_DEFERRED_H
#define KICH_R4_DEFERRED_H
#include "KichR4Stream.h"
/* Additive B4.4 contract. Old ABI tables and engine vtables stay unchanged.
 * Only numeric device-owned resource handles cross this pack(4) C interface.
 * begin_pass unbinds previous PS inputs before binding its output targets.
 * colors[0]==0 with color_count==1 selects the current swap-chain buffer.
 * Other color handles require COLOR_TARGET; depth requires DEPTH_TARGET.
 * All targets have identical dimensions; no MSAA, slices or mip selections.
 * Read/write aliasing is rejected. Reissue begin_pass after begin/resize/present.
 */
#define KICH_R4_DEFERRED_ABI UINT32_C(0x00020000)
#define KICH_R4_EXPORT_DEFERRED "KichR4_GetDeferredAPI"
enum KichR4PassClear {KICH_R4_CLEAR_COLOR=1,KICH_R4_CLEAR_DEPTH=2,KICH_R4_CLEAR_STENCIL=4};
#pragma pack(push,4)
typedef struct KichR4PassDesc {
    uint32_t struct_size,flags,color_count;
    KichR4Handle colors[4],depth;
    uint32_t width,height,clear_flags;
    float color[4],depth_value;
} KichR4PassDesc;
typedef struct KichR4DeferredDrawDesc {
    uint32_t struct_size;
    KichR4StreamDrawDesc state; /* state.texture is ignored; use textures. */
    KichR4Handle textures[8];  /* t0..t7, optional null slots. */
} KichR4DeferredDrawDesc;
typedef struct KichR4DeferredAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4PipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *begin_pass)(KichR4Handle,const KichR4PassDesc*);
    int32_t (KICH_R4_CALL *draw)(KichR4Handle,const KichR4DeferredDrawDesc*);
} KichR4DeferredAPI;
#pragma pack(pop)
typedef char KichR4PassDesc_size[(sizeof(KichR4PassDesc)==84)?1:-1];
typedef char KichR4DeferredDrawDesc_size[(sizeof(KichR4DeferredDrawDesc)==564)?1:-1];
#if defined(_M_IX86) || defined(__i386__)
typedef char KichR4DeferredAPI_x86_size[(sizeof(KichR4DeferredAPI)==28)?1:-1];
#endif
typedef int32_t (KICH_R4_CALL *KichR4GetDeferredAPIFn)(uint32_t,KichR4DeferredAPI*,uint32_t);
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetDeferredAPI(uint32_t,KichR4DeferredAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
