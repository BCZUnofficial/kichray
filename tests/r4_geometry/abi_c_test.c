#include <stddef.h>
#pragma pack(push,1)
#include "../../xr_3da/KichR4Geometry.h"
struct PackRestored {char a;uint32_t b;};
#pragma pack(pop)
_Static_assert(sizeof(struct PackRestored)==5,"caller packing restored");
_Static_assert(offsetof(KichR4GeometryAPI,create_pipeline)==16,"table prefix");
_Static_assert(offsetof(KichR4PipelineDesc,elements)==32,"input layout offset");
_Static_assert(offsetof(KichR4DrawIndexedDesc,index_count)==36,"draw packet offset");
_Static_assert(offsetof(KichR4GeometryReport,geometry)==80,"final ownership report");
int main(void){return 0;}
