#ifndef KICH_R4_DETAIL_H
#define KICH_R4_DETAIL_H
#include "KichR4Deferred.h"
/* G3a: two real IA streams. Mesh data is scene-owned; the instance stream is
 * dynamic WRITE_DISCARD, uploaded once and reused by geometry/shadow passes.
 * All resources use the existing single device registry and release_resource.
 * Borrowed input is copied before return. No engine/D3D/STL pointers in ABI. */
#define KICH_R4_DETAIL_ABI UINT32_C(0x00020000)
#define KICH_R4_MAX_DETAIL_INSTANCES UINT32_C(1048576)
#pragma pack(push,4)
typedef struct KichR4DetailVertex {float position[3],misc[4];} KichR4DetailVertex;
typedef struct KichR4DetailInstance {float row[3][4],lighting[4];} KichR4DetailInstance;
typedef struct KichR4DetailPipelineDesc {uint32_t struct_size;KichR4Handle vs,ps;} KichR4DetailPipelineDesc;
typedef struct KichR4DetailDrawDesc {
    uint32_t struct_size;
    KichR4DeferredDrawDesc surface;
    KichR4Handle instance_buffer;
    uint32_t first_instance,instance_count;
} KichR4DetailDrawDesc;
typedef struct KichR4DetailAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4DetailPipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *create_instances)(KichR4Handle,uint32_t,KichR4Handle*);
    int32_t (KICH_R4_CALL *upload_instances)(KichR4Handle,KichR4Handle,const KichR4DetailInstance*,uint32_t);
    int32_t (KICH_R4_CALL *draw)(KichR4Handle,const KichR4DetailDrawDesc*);
} KichR4DetailAPI;
#pragma pack(pop)
typedef char KichR4DetailVertex_size[(sizeof(KichR4DetailVertex)==28)?1:-1];
typedef char KichR4DetailInstance_size[(sizeof(KichR4DetailInstance)==64)?1:-1];
typedef char KichR4DetailDrawDesc_size[(sizeof(KichR4DetailDrawDesc)==584)?1:-1];
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetDetailAPI(uint32_t,KichR4DetailAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
