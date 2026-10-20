#include <stddef.h>
#pragma pack(push,1)
#include "../../xr_3da/KichR4Resources.h"
struct PackRestored { char a; uint32_t b; };
#pragma pack(pop)
_Static_assert(sizeof(struct PackRestored)==5,"ABI header restores caller packing");
_Static_assert(offsetof(KichR4ResourceAPI,create_device)==16,"table prefix");
_Static_assert(offsetof(KichR4ShaderVariable,name)==36,"reflection variable ABI");
_Static_assert(sizeof(KichR4Handle)==8,"fixed handle width");
int main(void) { return 0; }
