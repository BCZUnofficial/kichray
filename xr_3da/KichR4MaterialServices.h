#ifndef KICH_R4_MATERIAL_SERVICES_H
#define KICH_R4_MATERIAL_SERVICES_H
#include "KichR4Material.h"
// Registration is called from ResourceManager source-side metadata hooks. Find
// performs an in-memory lookup only; Resolve is reserved for level loading.
void KichR4_NativeRegisterMaterial(const void* element,uint32_t index,const char* shader,
    const char* textures,const char* origin,const char* bump,const char* detail,uint32_t usage,uint64_t class_id);
void KichR4_NativeForgetMaterial(const void* element);
const kich_r4_material::Descriptor* KichR4_NativeFindMaterial(const void* element,uint32_t pass);
bool KichR4_NativeResolveMaterial(const char* shader,const char* textures,kich_r4_material::Descriptor& out);
#endif
