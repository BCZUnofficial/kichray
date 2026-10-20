#include <stddef.h>
#pragma pack(push,1)
#include "../../xr_3da/KichR4Frame.h"
struct PackRestored {char a;uint32_t b;};
#pragma pack(pop)
_Static_assert(sizeof(struct PackRestored)==5,"caller packing restored");
_Static_assert(offsetof(KichR4FrameAPI,create_device)==16,"table prefix");
_Static_assert(offsetof(KichR4FrameStats,last_hresult)==44,"diagnostics ABI");
_Static_assert(sizeof(KichR4FrameStats)==52,"frame stats ABI");
int main(void){return 0;}
