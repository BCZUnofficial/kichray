#ifndef KICH_R4_BUILD_IDENTITY_H
#define KICH_R4_BUILD_IDENTITY_H
#include "KichR4Renderer.h"
/* MSBuild generates this in each project's IntDir before compilation. No
 * checked-in fallback: stale/missing identity must not masquerade as a match. */
#include "KichR4BuildIdentity.generated.h"
static inline KichR4BuildInfo KichR4_LocalBuildInfo() {
#if defined(_MSC_VER)
    const uint32_t compiler=_MSC_VER;
#elif defined(__GNUC__)
    const uint32_t compiler=__GNUC__*10000+__GNUC_MINOR__*100+__GNUC_PATCHLEVEL__;
#else
    const uint32_t compiler=0;
#endif
    const KichR4BuildInfo b={sizeof(b),sizeof(void*),KICH_R4_BUILD_DIRTY,compiler,
        KICH_R4_BUILD_REVISION,KICH_R4_BUILD_SOURCE_ID,KICH_R4_BUILD_SHADER_ID,
        __DATE__ " " __TIME__,KICH_R4_BUILD_CONFIGURATION};
    return b;
}
#endif
