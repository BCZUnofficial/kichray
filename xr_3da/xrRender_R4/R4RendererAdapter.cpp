/* G1 transitional provider. Pure C boundary; delegates to the working D3D11
 * services without adding a second device, registry, or resource owner. */
#include "../KichR4RendererContract.h"
#include "../KichR4BuildIdentity.h"

extern "C" int32_t KICH_R4_CALL KichR4_GetRendererAPI(uint32_t abi,KichR4RendererAPI* out,uint32_t bytes) {
    if(abi!=KICH_R4_RENDERER_ABI || !out || bytes<sizeof(*out))return KICH_R4_E_ARGUMENT;
    KichR4RendererAPI a={};
    a.struct_size=sizeof(a);a.abi_version=KICH_R4_RENDERER_ABI;
    a.pointer_size=sizeof(void*);a.flags=KICH_R4_RENDERER_TRANSITIONAL;
    a.build=KichR4_LocalBuildInfo();
    if(KichR4_GetBridgeVersion()!=KICH_R4_BRIDGE_ABI_VERSION ||
        KichR4_GetFrameAPI(KICH_R4_FRAME_ABI,&a.frame,sizeof(a.frame))!=KICH_R4_OK ||
        KichR4_GetResourceAPI(KICH_R4_RESOURCE_ABI,&a.resources,sizeof(a.resources))!=KICH_R4_OK ||
        KichR4_GetGeometryAPI(KICH_R4_GEOMETRY_ABI,&a.geometry,sizeof(a.geometry))!=KICH_R4_OK ||
        KichR4_GetLevelAPI(KICH_R4_LEVEL_ABI,&a.level,sizeof(a.level))!=KICH_R4_OK ||
        KichR4_GetStreamAPI(KICH_R4_STREAM_ABI,&a.stream,sizeof(a.stream))!=KICH_R4_OK ||
        KichR4_GetDeferredAPI(KICH_R4_DEFERRED_ABI,&a.deferred,sizeof(a.deferred))!=KICH_R4_OK ||
        KichR4_GetNativeToolsAPI(KICH_R4_NATIVE_TOOLS_ABI,&a.tools,sizeof(a.tools))!=KICH_R4_OK ||
        KichR4_ValidateRendererAPI(&a))return KICH_R4_E_STATE;
    *out=a;
    return KICH_R4_OK;
}
