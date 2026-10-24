#ifndef KICH_R4_MODEL_H
#define KICH_R4_MODEL_H
#include "KichR4Deferred.h"
/* G3c GPU model contract. Immutable indexed source geometry uses the existing
 * registry. A palette class is shared only by validated identical poses of
 * one skeleton in one capture frame, independently of world transforms.
 * No engine/STL/D3D pointers cross this boundary; upload_pose consumes only
 * its 16-byte header and bone_count matrices before returning. */
#define KICH_R4_MODEL_ABI UINT32_C(0x00030000)
#define KICH_R4_MAX_MODEL_BONES UINT32_C(256)
#define KICH_R4_MODEL_POSE_BUDGET_BYTES UINT32_C(16777216)
#define KICH_R4_MAX_MODEL_POSE_SLOTS UINT32_C(16384)
#define KICH_R4_MODEL_CAPACITY_CLASSES UINT32_C(5)
/* b1 contains a uint4 header followed by exactly the declared matrix class.
 * A shader and its bound palette must have the same class, including rigid
 * draws. The maximum-sized CPU staging type is never copied wholesale. */
static inline uint32_t KichR4ModelValidCapacity(uint32_t capacity){return capacity==16 || capacity==32 || capacity==64 || capacity==128 || capacity==256;}
static inline uint32_t KichR4ModelPaletteCapacity(uint32_t bones){uint32_t capacity=16;if(bones>256)return 0;while(capacity<bones)capacity*=2;return capacity;}
static inline uint32_t KichR4ModelPaletteBytes(uint32_t capacity){return KichR4ModelValidCapacity(capacity)?16+64*capacity:0;}
#pragma pack(push,4)
typedef struct KichR4ModelVertex {
    float position[3],uv[2],weight;uint16_t bones[2];uint32_t normal,tangent,binormal;
    float uv1[2],hemi,sun;uint32_t color,attributes;
} KichR4ModelVertex;
typedef struct KichR4ModelPose {
    uint32_t bone_count,skin_mode,skinned,reserved;float bones[256][16];
} KichR4ModelPose;
typedef struct KichR4ModelPipelineDesc {uint32_t struct_size;KichR4Handle vs,ps;uint32_t bone_capacity;} KichR4ModelPipelineDesc;
typedef struct KichR4ModelDrawDesc {
    uint32_t struct_size;KichR4DeferredDrawDesc surface;KichR4Handle pose_buffer;
} KichR4ModelDrawDesc;
typedef struct KichR4ModelAPI {
    uint32_t struct_size,abi_version,pointer_size,flags;
    int32_t (KICH_R4_CALL *create_pipeline)(KichR4Handle,const KichR4ModelPipelineDesc*,KichR4Handle*);
    int32_t (KICH_R4_CALL *create_pose)(KichR4Handle,uint32_t,KichR4Handle*);
    int32_t (KICH_R4_CALL *upload_pose)(KichR4Handle,KichR4Handle,const KichR4ModelPose*);
    int32_t (KICH_R4_CALL *draw)(KichR4Handle,const KichR4ModelDrawDesc*);
} KichR4ModelAPI;
#pragma pack(pop)
typedef char KichR4ModelVertex_size[(sizeof(KichR4ModelVertex)==64)?1:-1];
typedef char KichR4ModelPose_size[(sizeof(KichR4ModelPose)==16400)?1:-1];
typedef char KichR4ModelDrawDesc_size[(sizeof(KichR4ModelDrawDesc)==576)?1:-1];
#ifdef __cplusplus
extern "C" {
#endif
int32_t KICH_R4_CALL KichR4_GetModelAPI(uint32_t,KichR4ModelAPI*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
