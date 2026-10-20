#include <stddef.h>
#pragma pack(push,1)
#include "../../xr_3da/KichR4Level.h"
struct PackRestored {char a;uint32_t b;};
#pragma pack(pop)
_Static_assert(sizeof(struct PackRestored)==5,"caller packing restored");
_Static_assert(offsetof(KichR4LevelAPI,create_texture_dds)==16,"table prefix");
_Static_assert(offsetof(KichR4TexturedDrawDesc,texture)==56,"texture handle offset");
int main(void){return 0;}
