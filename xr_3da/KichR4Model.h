#ifndef KICH_R4_MODEL_H
#define KICH_R4_MODEL_H
#include "KichR4Deferred.h"
/* G3b additive GPU model contract. Immutable indexed source geometry uses the
 * existing resource registry. A validated pose buffer is uploaded once per
 * instance/frame and reused by all camera/light passes. No engine/STL/D3D
 * pointers cross this boundary; all buffers are copied before return. */
#define KICH_R4_MODEL_ABI UINT32_C(0x00010000)
#define KICH_R4_MAX_MODEL_BONES UINT32_C(256)
#define KICH_R4_MODEL_POSE_BUDGET_BYTES UINT32_C(16777216)
#define KICH_R4_MAX_MODEL_POSE_SLOTS UINT32_C(1023)
#pragma pack(push,4)
typedef struct KichR4ModelVertex {
    float position[3],uv[2],weight;uint16_t bones[2];uint32_t normal;
} KichR4ModelVertex;
typedef struct KichR4ModelPose {
    float bones[256][16];uint32_t bone_count,skin_mode,skinned,reserved;
} KichR4ModelPose;
typedef struct KichR4ModelPipelineDesc {uint32_t struct_size;KichR4Handle vs,ps;} KichR4ModelPipelineDesc;
typedef struct KichR4ModelDrawDesc {
    uint32_t struct_size;KichR4DeferredDrawDesc surface;KichR4Handle pose_buffer;
} KichR4ModelDrawDesc;
typedef struct KichR4ModelAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4ModelPipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *create_pose)(KichR4Handle,KichR4Handle*);
    int32_t (KICH_R4_CALL *upload_pose)(KichR4Handle,KichR4Handle,const KichR4ModelPose*);
    int32_t (KICH_R4_CALL *draw)(KichR4Handle,const KichR4ModelDrawDesc*);
} KichR4ModelAPI;
#pragma pack(pop)
typedef char KichR4ModelVertex_size[(sizeof(KichR4ModelVertex)==32)?1:-1];
typedef char KichR4ModelPose_size[(sizeof(KichR4ModelPose)==16400)?1:-1];
typedef char KichR4ModelDrawDesc_size[(sizeof(KichR4ModelDrawDesc)==192)?1:-1];
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetModelAPI(uint32_t,KichR4ModelAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
