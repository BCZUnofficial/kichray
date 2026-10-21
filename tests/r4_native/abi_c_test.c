#include "../../xr_3da/KichR4RendererContract.h"
#include <stddef.h>
int main(void) {
    KichR4RendererAPI a={0};
    return sizeof(KichR4BuildInfo)!=288 || offsetof(KichR4RendererAPI,build)!=16 ||
        offsetof(KichR4RendererAPI,frame)!=304 || !KichR4_ValidateRendererAPI(&a) ||
        KichR4_BuildInfoValid(NULL) || KichR4_BuildsMatch(NULL,NULL);
}
