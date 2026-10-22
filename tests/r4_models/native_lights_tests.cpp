#include "../../xr_3da/KichR4Lighting.h"
#include <map>
#include <cstring>
#include <new>
#include <stdexcept>
#include <stdio.h>
#define ZeroMemory(p,n) std::memset(p,0,n)
#define CopyMemory std::memcpy
#define PI_DIV_2 1.57079632679f
typedef const char* LPCSTR;
struct Fvector {float x,y,z;};
struct Fcolor {float r,g,b,a;};
struct IRender_Light {enum LT{POINT=1,SPOT=2};};
struct Source {KichR4LightIdentity identity;KichR4LocalLight light;bool active,shadow;};
std::map<const void*,Source> registry;
void KichR4_CaptureLightIdentity(const void* id,const KichR4LightIdentity& source){registry[id].identity=source;}
void KichR4_CaptureLight(const void* id,const KichR4LocalLight& light,bool active){registry[id].light=light;registry[id].active=active;}
void KichR4_CaptureLightShadow(const void* id,bool shadow){registry[id].shadow=shadow;}
void KichR4_CaptureLightForget(const void* id){registry.erase(id);}
#include "native_light_under_test.cpp"
namespace {unsigned checks=0;void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}}
int main(){try{
    alignas(R4Light) unsigned char storage[sizeof(R4Light)];R4Light* runtime=new(storage) R4Light;
    runtime->set_active(true);const KichR4LightIdentity first=registry[runtime].identity;
    Require(first.role==KICH_R4_LIGHT_ROLE_RUNTIME && first.serial && first.level_record==UINT32_MAX && first.controller==UINT32_MAX,"runtime role has explicit unknown level provenance");
    Require(first.process_handle==static_cast<uint64_t>(reinterpret_cast<uintptr_t>(runtime)),"pointer identity is explicit process-local source handle");
    runtime->set_active(false);runtime->set_active(true);
    Require(registry[runtime].identity.serial==first.serial,"activation changes preserve source lifetime serial");
    R4Light other;other.set_active(true);
    Require(registry.size()==2 && registry[&other].identity.serial!=first.serial && !std::memcmp(&registry[&other].light,&registry[runtime].light,sizeof(KichR4LocalLight)),"numerically identical sources retain distinct identities");
    other.SetLevelIdentity(12,7);
    Require(registry[&other].identity.role==KICH_R4_LIGHT_ROLE_LEVEL && registry[&other].identity.level_record==12 && registry[&other].identity.controller==7,"level record ordinal and controller preserved");
    runtime->~R4Light();Require(registry.count(runtime)==0,"destruction unregisters source");
    runtime=new(storage) R4Light;runtime->set_active(true);
    Require(registry[runtime].identity.process_handle==first.process_handle && registry[runtime].identity.serial>first.serial,"address reuse starts a new source lifetime");
    runtime->~R4Light();printf("G3b production native light provenance: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
