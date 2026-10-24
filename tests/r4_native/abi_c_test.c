#include "../../xr_3da/KichR4RendererContract.h"
#include <stddef.h>
_Static_assert(sizeof(KichR4ModelVertex)==64,"immutable model vertex ABI");
_Static_assert(offsetof(KichR4ModelVertex,tangent)==32,"model tangent offset");
_Static_assert(sizeof(KichR4ModelPose)==16400,"maximum CPU pose staging ABI");
_Static_assert(offsetof(KichR4ModelPose,bones)==16,"GPU palette header precedes matrices");
_Static_assert(sizeof(KichR4ModelPipelineDesc)==24,"model capacity pipeline descriptor ABI");
_Static_assert(sizeof(KichR4ModelAPI)==16+4*sizeof(void*),"model function table ABI");
_Static_assert(sizeof(KichR4ModelDrawDesc)==672,"model separate material inputs ABI");
_Static_assert(offsetof(KichR4ModelDrawDesc,material_textures)==576,"model-only t8 t9 offset");
_Static_assert(offsetof(KichR4ModelDrawDesc,material_samplers)==592,"model-only s9 s10 offset");
_Static_assert(KICH_R4_MODEL_ABI==0x00040000,"separate model material inputs handshake version");
int main(void) {
    KichR4RendererAPI a={0};
    return sizeof(KichR4BuildInfo)!=288 || offsetof(KichR4RendererAPI,build)!=16 ||
        offsetof(KichR4RendererAPI,frame)!=304 || !KichR4_ValidateRendererAPI(&a) ||
        KichR4_BuildInfoValid(NULL) || KichR4_BuildsMatch(NULL,NULL);
}
