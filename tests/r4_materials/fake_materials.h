#ifndef FAKE_MATERIALS_H
#define FAKE_MATERIALS_H
#include "../../xr_3da/RenderSnapshotMaterial.h"
#include "../../xr_3da/RenderSnapshotMaterialData.h"
#include "../../xr_3da/RenderSnapshotGeometryData.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/KichR4MaterialServices.h"
inline void KichR4_NativeRegisterMaterial(const void*,uint32_t,const char*,const char*,const char*,const char*,const char*,uint32_t,uint64_t){}
inline void KichR4_NativeForgetMaterial(const void*){}
struct D3DVERTEXELEMENT9 {uint16_t Stream,Offset;uint8_t Type,Method,Usage,UsageIndex;};
enum {D3DDECLTYPE_FLOAT3=2,D3DDECLTYPE_FLOAT4=3,D3DDECLTYPE_D3DCOLOR=4};
extern uint32_t effect;
template<class T>struct Ref {T* p;Ref(T* v=NULL):p(v){}T* _get()const{return p;}T* operator->()const{return p;}T& operator*()const{return *p;}operator bool()const{return p!=NULL;}};
struct CTexture {std::string cName;void* pSurface;CTexture(const char* name):cName(name),pSurface(NULL){}};
struct ShaderStage {std::string cName;ShaderStage(const char* name):cName(name){}};
typedef std::vector<std::pair<uint32_t,Ref<CTexture> > > TextureList;
enum {D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ZFUNC=23,D3DRS_ALPHATESTENABLE=15,D3DRS_ALPHAFUNC=25,D3DRS_ALPHAREF=24,D3DRS_ALPHABLENDENABLE=27,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_CULLMODE=22,D3DRS_COLORWRITEENABLE=168,D3DRS_STENCILENABLE=52,D3DRS_SCISSORTESTENABLE=174};
struct FakeStates {std::map<uint32_t,uint32_t> values;uint32_t get_RS(uint32_t key,uint32_t fallback)const{auto i=values.find(key);return i==values.end()?fallback:i->second;}};
struct FakeState {FakeStates state_code;};
struct SPass {Ref<FakeState> state;Ref<ShaderStage> vs,ps;Ref<TextureList> T;};
struct ShaderElement {struct Flags {bool bDistort=false,bEmissive=false,bWmark=false,bStrictB2F=false;uint32_t iPriority=0;}flags;std::vector<Ref<SPass> > passes;};
inline bool KichR4_NativeActive(){return true;}
snapshot_material::Registry& MaterialRegistry();
extern uint64_t material_metadata_errors;
void MaterialError();
void SnapshotGeneratedGeometry(const D3DVERTEXELEMENT9*,uint32_t,uint32_t,uint32_t,const unsigned char*,const std::map<uint32_t,uint32_t>&,
    const D3DVERTEXELEMENT9*,const D3DVERTEXELEMENT9*,const D3DVERTEXELEMENT9*,const D3DVERTEXELEMENT9*,const kich_r4_capture::Batch&,uint32_t,
    const kich_r4_scene::Matrix&,const kich_r4_scene::Matrix&,bool,const kich_r4_capture::Command&);
#endif
